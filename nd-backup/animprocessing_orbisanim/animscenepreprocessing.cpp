/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent stictly prohibited
 */

#include "icelib/animprocessing_orbisanim/animprocessing.h"
#include "icelib/itscene/studiopolicy.h"
#include "icelib/itscene/itjointanim.h"
#include "icelib/itscene/itanim.h"

namespace OrbisAnim {
	namespace Tools {
		namespace AnimProcessing {

//-----------------
extern float g_fEpsilonScale;
extern float g_fEpsilonRotation;
extern float g_fEpsilonTranslation;
extern float g_fEpsilonFloat;

extern std::string UnmangleAttributeName(std::string const& fullDagPath, ITSCENE::SceneDb const& animScene);
/*
ITSCENE::JointAnimation* ExtractJointAnimation( ITSCENE::JointAnimation const* pJointAnim, ITSCENE::Joint const *pBindPoseJoint, OutputAnimSpec const& outputAnimSpec)
{
	ITSCENE::JointAnimation *pNewJointAnim = new ITSCENE::JointAnimation( pJointAnim->m_blend, pJointAnim->m_compression );

	ITSCENE::Joint const* pRefJoint = NULL;
	ITSCENE::JointAnimation const* pRefJointAnim = NULL;
	if (outputAnimSpec.m_subtractSpec.m_pAnimScene) {
		ITSCENE::SceneDb const& refAnimScene = *outputAnimSpec.m_subtractSpec.m_pAnimScene;
		size_t numRefJointAnims = refAnimScene.GetNumJointAnimations();
		std::string const& jointName = pBindPoseJoint->m_name;
		for (size_t iRefJointAnim = 0; iRefJointAnim < numRefJointAnims; ++iRefJointAnim) {
			pRefJointAnim = refAnimScene.m_jointAnimations[iJointAnim];
			std::string const& jointAnimName = g_theStudioPolicy->UnmangleName(pRefJointAnim->TargetPathString(), refAnimScene);
			if (jointAnimName == jointName)
				break;
			pRefJointAnim = NULL;
		}
		if (!pRefJointAnim || !pRefJointAnim->HasSamples()) {
			IWARN("Joint animation '%s' has no corresponding joint animation in reference scene - using bind pose...\n", jointName.c_str());
			pRefJoint = pBindPoseJoint;
		}
	} else if (outputAnimSpec.m_subtractSpec.IsBindPose())
		pRefJoint = pBindPoseJoint;

	if (pRefJointAnim) {
		float fFrameRate = outputAnimSpec.m_fFrameRate;
		unsigned numSamples = (unsigned)pJointAnim->GetNumSamplesInTrack( ITSCENE::kAnimTrackScaleX );
		if (fFrameRate == 0.0f && pJointAnim->m_duration > 0.0f) {
			fFrameRate = (float)numSamples / pJointAnim->m_duration;
			fFrameRate = (float)(int)(fFrameRate + 0.5f);		// round to integer, since we know Maya always uses integral frame rates
		}
		float fRefFrameRate = outputAnimSpec.m_subtractSpec.m_fFrameRate;
		unsigned numRefSamples = (unsigned)pRefJointAnim->GetNumSamplesInTrack( ITSCENE::kAnimTrackScaleX );
		if (fRefFrameRate == 0.0f && pRefJointAnim->m_duration > 0.0f) {
			fRefFrameRate = (float)numRefSamples / pRefJointAnim->m_duration;
			fRefFrameRate = (float)(int)(fRefFrameRate + 0.5f);	// round to integer, since we know Maya always uses integral frame rates
		}
		unsigned iRefStartFrame = ((unsigned)outputAnimSpec.m_subtractSpec.m_startFrame > numRefSamples-1) ? numRefSamples-1 : outputAnimSpec.m_subtractSpec.m_startFrame;
		unsigned iRefEndFrame = ((unsigned)outputAnimSpec.m_subtractSpec.m_endFrame > numRefSamples) ? numRefSamples : outputAnimSpec.m_subtractSpec.m_endFrame;
		unsigned numRefFrames = iRefEndFrame - iRefStartFrame;

		float fRefFrameStep = (fFrameRate > 0.0f) ? outputAnimSpec.m_fSubtractFrameSpeed * fRefFrameRate / fFrameRate : 0.0f;
		int iStartFrame = outputAnimSpec.m_subtractFrameOffset + 
	} else if (pRefJoint) {

	} else {

	}

	return pNewJointAnim;
}

ITSCENE::SceneDb* ExtractAnimScene(	OutputAnimSpec const& outputAnimSpec, ITSCENE::SceneDb const& bindPoseScene)
{
	if (!outputAnimSpec.m_pAnimScene || (outputAnimSpec.m_pAnimScene->m_flags & ITSCENE::BSF_EULER2QUAT) == 0) {
		ITASSERT(outputAnimSpec.m_pAnimScene && (outputAnimSpec.m_pAnimScene->m_flags & ITSCENE::BSF_EULER2QUAT) != 0);
		return NULL;
	}
	ITSCENE::SceneDb const& animScene = *outputAnimSpec.m_pAnimScene;

	ITSCENE::SceneDb* pNewAnimScene = new ITSCENE::SceneDb;

	size_t numJointAnims = animScene.GetNumJointAnimations();
	for (size_t iJointAnim = 0; iJointAnim < numJointAnims; ++iJointAnim) {
		ITSCENE::JointAnimation const* pJointAnim = animScene.m_jointAnimations[iJointAnim];
		// For each joint animation, check if there is a corresponding bind pose joint channel:
		ITSCENE::Joint const *pBindPoseJoint = NULL;
		std::string const jointAnimName = g_theStudioPolicy->UnmangleName(pJointAnim->TargetPathString(), animScene);
		for (unsigned iJoint = 0; iJoint < (unsigned)bindPoseScene.GetNumJoints(); ++iJoint) {
			ITSCENE::Joint const *pJoint = bindPoseScene.GetJoint(iJoint);
			std::string const& jointName = pJoint->m_name;
			if (jointAnimName == jointName) {
				pBindPoseJoint = pJoint;
				break;
			}
		}
		if (!pBindPoseJoint) {
			IWARN("Joint animation '%s' has no correspoding joint in bind pose scene - ignoring...\n", jointAnimName.c_str());
			continue;
		}

		// create a new joint animation from the specified range of joints
		ITSCENE::JointAnimation *pNewJointAnim = ExtractJointAnimation( pJointAnim, pBindPoseJoint, outputAnimSpec );
		pOutputAnimScene->m_jointAnimations.push_back(pNewJointAnim);
	}
}
*/
void ExtractAdditiveAnimUsingBindPose(ITSCENE::SceneDb& animScene, ITSCENE::SceneDb const& bindPoseScene)
{
	ITASSERT((animScene.m_flags & ITSCENE::BSF_EULER2QUAT) != 0);

	size_t numJointAnims = animScene.GetNumJointAnimations();
	for (size_t iJointAnim = 0; iJointAnim < numJointAnims; )
	{
		ITSCENE::JointAnimation * anim = animScene.m_jointAnimations[iJointAnim];
		ITSCENE::Joint const *bindPoseJoint = NULL;
		std::string const jointAnimName = g_theStudioPolicy->UnmangleName(anim->TargetPathString(), animScene);

		for (unsigned boneIndex = 0; boneIndex < (unsigned)bindPoseScene.GetNumJoints(); boneIndex++)
		{
			ITSCENE::Joint const *joint = bindPoseScene.GetJoint(boneIndex);
			std::string jointName = joint->m_name;
			if (jointAnimName == jointName) {
				bindPoseJoint = joint;
				break;
			}
		}

		if (!bindPoseJoint)	{
			IWARN("Joint animation '%s' has no correspoding joint in bind pose scene - ignoring...\n", jointAnimName.c_str());
			animScene.m_jointAnimations.erase( animScene.m_jointAnimations.begin() + iJointAnim );
			numJointAnims = animScene.GetNumJointAnimations();
			continue;
		}

		// get scale, trans and rotation from the joint's matrix
		ITGEOM::Vec3 scaleInv, transInv;
		ITGEOM::Quat quatInv;
		{
			ITGEOM::Vec3 scale, trans;
			ITGEOM::Quat quat;
			ITGEOM::Matrix4x4 const &mat = bindPoseJoint->m_transformMatrix;
			ITGEOM::Vec3 xAxis = mat.GetRow(0);
			ITGEOM::Vec3 yAxis = mat.GetRow(1);
			ITGEOM::Vec3 zAxis = mat.GetRow(2);
			scale = ITGEOM::Vec3( Length( xAxis ), Length( yAxis ), Length( zAxis ) );
			scaleInv = 1.0f / scale;
			xAxis *= scaleInv.x;
			yAxis *= scaleInv.y;
			zAxis *= scaleInv.z;
			ITGEOM::Matrix3x3 rot( xAxis, yAxis, zAxis );
			QuatFromMatrix3x3(&quat, &rot);
			if (quat.w < 0.0f)
				quat *= -1.0f;
			quatInv = quat.Conjugate();
			trans = mat.GetRow(3);
			transInv = -trans;
		}

		ITDATAMGR::FloatArray samples_x, samples_y, samples_z, samples_w;
		bool bIdentity = true;

		// handle scales
		size_t samplesN = anim->GetNumSamplesInTrack( ITSCENE::kAnimTrackScaleX );
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackScaleX, samples_x);
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackScaleY, samples_y);
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackScaleZ, samples_z);
		for (size_t i=0; i<samplesN; ++i) {
			ITGEOM::Vec3 scale(samples_x[i], samples_y[i], samples_z[i]);
			scale *= scaleInv;
			samples_x[i] = scale.x;
			samples_y[i] = scale.y;
			samples_z[i] = scale.z;
			bIdentity = bIdentity && scale.IsEquiv(kIdentityScale, g_fEpsilonScale);
		}
		anim->SetSamples(ITSCENE::kAnimTrackScaleX, samples_x);
		anim->SetSamples(ITSCENE::kAnimTrackScaleY, samples_y);
		anim->SetSamples(ITSCENE::kAnimTrackScaleZ, samples_z);

		// handle rotations
		samplesN = anim->GetNumSamplesInTrack( ITSCENE::kAnimTrackRotateX );
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackRotateX, samples_x);
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackRotateY, samples_y);
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackRotateZ, samples_z);
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackRotateW, samples_w);

		for (size_t i=0; i<samplesN; ++i) {
			ITGEOM::Quat rot(samples_x[i], samples_y[i], samples_z[i], samples_w[i]);
			rot = quatInv * rot;
			if (rot.w < 0.0f)
				rot *= -1.0f;
			samples_x[i] = rot.x;
			samples_y[i] = rot.y;
			samples_z[i] = rot.z;
			samples_w[i] = rot.w;
			bIdentity = bIdentity && rot.IsEquiv(kIdentityRotation, g_fEpsilonRotation);
		}
		anim->SetSamples(ITSCENE::kAnimTrackRotateX, samples_x);
		anim->SetSamples(ITSCENE::kAnimTrackRotateY, samples_y);
		anim->SetSamples(ITSCENE::kAnimTrackRotateZ, samples_z);
		anim->SetSamples(ITSCENE::kAnimTrackRotateW, samples_w);

		// handle translations
		samplesN = anim->GetNumSamplesInTrack( ITSCENE::kAnimTrackTranslateX );
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackTranslateX, samples_x);
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackTranslateY, samples_y);
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackTranslateZ, samples_z);
		for (size_t i=0; i<samplesN; ++i)
		{
			ITGEOM::Vec3 trans(samples_x[i], samples_y[i], samples_z[i]);
			trans = quatInv.RotateVector(trans + transInv) * scaleInv;
			samples_x[i] = trans.x;
			samples_y[i] = trans.y;
			samples_z[i] = trans.z;
			bIdentity = bIdentity && trans.IsEquiv(kIdentityTranslation, g_fEpsilonTranslation);
		}
		anim->SetSamples(ITSCENE::kAnimTrackTranslateX, samples_x);
		anim->SetSamples(ITSCENE::kAnimTrackTranslateY, samples_y);
		anim->SetSamples(ITSCENE::kAnimTrackTranslateZ, samples_z);

		if (bIdentity) {
			// Delete any additive identity tracks
			INOTE("Deleting identity joint anim '%s' from additive animation\n", jointAnimName.c_str());
			animScene.m_jointAnimations.erase( animScene.m_jointAnimations.begin() + iJointAnim );
			numJointAnims = animScene.GetNumJointAnimations();
		} else {
			++iJointAnim;
		}
	}

	size_t numFloatAnims = animScene.GetNumFloatAttributeAnims();
	for (size_t iFloatAnim = 0; iFloatAnim < numFloatAnims; )
	{
		ITSCENE::FloatAttributeAnim *anim = animScene.m_floatAttributeAnims[iFloatAnim];
		ITSCENE::FloatAttribute const *bindPoseFloat = NULL;
		std::string const floatAnimName = UnmangleAttributeName(anim->TargetPathString(), animScene);
		for (unsigned floatAttributeIndex = 0; floatAttributeIndex < bindPoseScene.GetNumFloatAttributes(); floatAttributeIndex++) {
			ITSCENE::FloatAttribute const *sceneFloat = bindPoseScene.GetFloatAttribute((int)floatAttributeIndex);
			if (!sceneFloat->IsType(ITSCENE::kFaAnimOutput))
				continue;
			std::string floatChannelName = sceneFloat->GetFullName();
			if (floatAnimName == floatChannelName) {
				bindPoseFloat = sceneFloat;
				break;
			}
		}
		if (!bindPoseFloat) {
			IWARN("Float animation '%s' has no corresponding float channel in bind pose scene - ignoring...\n", floatAnimName.c_str());
			animScene.m_floatAttributeAnims.erase( animScene.m_floatAttributeAnims.begin() + iFloatAnim );
			numFloatAnims = animScene.GetNumFloatAttributeAnims();
			continue;
		}

		bool bIdentity = true;

		// get value from the bind pose float channel
		float value = bindPoseFloat->GetDefaultValue();
		// subtract bind pose value from all samples of float channel
		size_t samplesN = anim->GetNumSamples();
		ITDATAMGR::FloatArray samples;
		anim->GetSamples( samples );
		for (size_t i=0; i<samplesN; ++i) {
			samples[i] -= value;
			bIdentity = bIdentity && fabsf(samples[i]) <= g_fEpsilonFloat;
		}
		anim->SetSamples(samples);

		if (bIdentity) {
			// Delete any additive identity tracks
			INOTE("Deleting identity float attribute anim '%s' from additive animation\n", anim->TargetPathString().c_str());
			animScene.m_floatAttributeAnims.erase( animScene.m_floatAttributeAnims.begin() + iFloatAnim );
			numFloatAnims = animScene.GetNumFloatAttributeAnims();
		} else {
			++iFloatAnim;
		}
	}

	animScene.m_flags |= ITSCENE::BSF_ADDITIVE;
}

void ExtractAdditiveAnimUsingReferenceAnimation(ITSCENE::SceneDb& animScene, ITSCENE::SceneDb const& refScene, int startFrame, bool useOnlyStartFrame)
{
	ITASSERT((animScene.m_flags & ITSCENE::BSF_EULER2QUAT) != 0);
	ITASSERT((refScene.m_flags & ITSCENE::BSF_EULER2QUAT) != 0);

	bool isRefLooping = false;
	{
		std::string looping;
		refScene.GetInfo( &looping, "loopingAnim" );
		isRefLooping = (looping != "");
	}

	size_t numJointAnims = animScene.GetNumJointAnimations();
	for (size_t iJointAnim = 0; iJointAnim < numJointAnims; )
	{
		ITSCENE::JointAnimation * anim = animScene.m_jointAnimations[iJointAnim];
		ITSCENE::JointAnimation const *refJointAnimation = NULL;
		std::string const& jointAnimName = anim->TargetPathString();

		// search the reference scene for the corresponding animation
		for (size_t animBoneIndex=0; animBoneIndex < refScene.GetNumJointAnimations(); animBoneIndex++) {
			if (jointAnimName == refScene.m_jointAnimations[animBoneIndex]->TargetPathString()) {
				refJointAnimation = refScene.m_jointAnimations[animBoneIndex];
				break;
			}
		}
		if (!refJointAnimation) {
			IWARN("Joint animation '%s' has no corresponding joint in reference scene - ignoring...\n", jointAnimName.c_str());
			animScene.m_jointAnimations.erase( animScene.m_jointAnimations.begin() + iJointAnim );
			numJointAnims = animScene.GetNumJointAnimations();
			continue;
		}

		bool bIdentity = true;

		ITDATAMGR::FloatArray refsamples_tx, refsamples_ty, refsamples_tz;
		ITDATAMGR::FloatArray refsamples_rx, refsamples_ry, refsamples_rz, refsamples_rw;
		ITDATAMGR::FloatArray refsamples_sx, refsamples_sy, refsamples_sz;
		refJointAnimation->NodeAnimation::GetSamples(ITSCENE::kAnimTrackScaleX, refsamples_sx);
		refJointAnimation->NodeAnimation::GetSamples(ITSCENE::kAnimTrackScaleY, refsamples_sy);
		refJointAnimation->NodeAnimation::GetSamples(ITSCENE::kAnimTrackScaleZ, refsamples_sz);
		int const numRefFrames = (int)refsamples_sx.size();

		ITDATAMGR::FloatArray samples_x, samples_y, samples_z, samples_w;

		// handles scales
		size_t samplesN = anim->GetNumSamplesInTrack( ITSCENE::kAnimTrackScaleX );
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackScaleX, samples_x);
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackScaleY, samples_y);
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackScaleZ, samples_z);
		for (size_t i=0; i<samplesN; ++i) {
			int refFrame = useOnlyStartFrame ? startFrame : startFrame + (int)i;
			if (refFrame < 0) {
				refFrame = (isRefLooping) ? numRefFrames - ((numRefFrames - refFrame) % numRefFrames) : 0;
			} else if (refFrame >= (int)numRefFrames) {
				refFrame = (isRefLooping) ? (refFrame % numRefFrames) : numRefFrames-1;
			}
			//check if all scales are equal
			samples_x[i] /= refsamples_sx[refFrame];
			samples_y[i] /= refsamples_sy[refFrame];
			samples_z[i] /= refsamples_sz[refFrame];
			bIdentity = bIdentity &&
						samples_x[i] <= kIdentityScale.x+g_fEpsilonScale && samples_x[i] >= kIdentityScale.x-g_fEpsilonScale &&
						samples_y[i] <= kIdentityScale.y+g_fEpsilonScale && samples_y[i] >= kIdentityScale.y-g_fEpsilonScale &&
						samples_z[i] <= kIdentityScale.z+g_fEpsilonScale && samples_z[i] >= kIdentityScale.z-g_fEpsilonScale;
		}
		anim->SetSamples(ITSCENE::kAnimTrackScaleX, samples_x);
		anim->SetSamples(ITSCENE::kAnimTrackScaleY, samples_y);
		anim->SetSamples(ITSCENE::kAnimTrackScaleZ, samples_z);

		refJointAnimation->NodeAnimation::GetSamples(ITSCENE::kAnimTrackRotateX, refsamples_rx);
		refJointAnimation->NodeAnimation::GetSamples(ITSCENE::kAnimTrackRotateY, refsamples_ry);
		refJointAnimation->NodeAnimation::GetSamples(ITSCENE::kAnimTrackRotateZ, refsamples_rz);
		refJointAnimation->NodeAnimation::GetSamples(ITSCENE::kAnimTrackRotateW, refsamples_rw);

		//handle rotations
		samplesN = anim->GetNumSamplesInTrack( ITSCENE::kAnimTrackRotateX );
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackRotateX, samples_x);
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackRotateY, samples_y);
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackRotateZ, samples_z);
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackRotateW, samples_w);
		for (size_t i=0; i<samplesN; ++i)
		{
			int refFrame = useOnlyStartFrame ? startFrame : startFrame + (int)i;
			if (refFrame < 0) {
				refFrame = (isRefLooping) ? numRefFrames - ((numRefFrames - refFrame) % numRefFrames) : 0;
			} else if (refFrame >= (int)numRefFrames) {
				refFrame = (isRefLooping) ? (refFrame % numRefFrames) : numRefFrames-1;
			}
			ITGEOM::Quat rot(samples_x[i], samples_y[i], samples_z[i], samples_w[i]);
			ITGEOM::Quat q(refsamples_rx[refFrame], refsamples_ry[refFrame], refsamples_rz[refFrame], refsamples_rw[refFrame]);
			rot = q.Conjugate() * rot;
			if (rot.w < 0.0f)
				rot *= -1.0f;
			samples_x[i] = rot.x;
			samples_y[i] = rot.y;
			samples_z[i] = rot.z;
			samples_w[i] = rot.w;
			bIdentity = bIdentity && rot.IsEquiv(kIdentityRotation, g_fEpsilonRotation);
		}
		anim->SetSamples(ITSCENE::kAnimTrackRotateX, samples_x);
		anim->SetSamples(ITSCENE::kAnimTrackRotateY, samples_y);
		anim->SetSamples(ITSCENE::kAnimTrackRotateZ, samples_z);
		anim->SetSamples(ITSCENE::kAnimTrackRotateW, samples_w);

		// handle translations
		refJointAnimation->NodeAnimation::GetSamples(ITSCENE::kAnimTrackTranslateX, refsamples_tx);
		refJointAnimation->NodeAnimation::GetSamples(ITSCENE::kAnimTrackTranslateY, refsamples_ty);
		refJointAnimation->NodeAnimation::GetSamples(ITSCENE::kAnimTrackTranslateZ, refsamples_tz);
		samplesN = anim->GetNumSamples( ITSCENE::kTranslateXMask );
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackTranslateX, samples_x);
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackTranslateY, samples_y);
		anim->NodeAnimation::GetSamples(ITSCENE::kAnimTrackTranslateZ, samples_z);
		for (size_t i=0; i<samplesN; ++i) {
			int refFrame = useOnlyStartFrame ? startFrame : startFrame + (int)i;
			if (refFrame < 0) {
				refFrame = (isRefLooping) ? numRefFrames - ((numRefFrames - refFrame) % numRefFrames) : 0;
			} else if (refFrame >= (int)numRefFrames) {
				refFrame = (isRefLooping) ? (refFrame % numRefFrames) : numRefFrames-1;
			}
			ITGEOM::Vec3 trans(samples_x[i] - refsamples_tx[refFrame], samples_y[i] - refsamples_ty[refFrame], samples_z[i] - refsamples_tz[refFrame]);
			ITGEOM::Quat q(refsamples_rx[refFrame], refsamples_ry[refFrame], refsamples_rz[refFrame], refsamples_rw[refFrame]);
			ITGEOM::Vec3 scale(refsamples_sx[refFrame], refsamples_sy[refFrame], refsamples_sz[refFrame]);
			trans = q.Conjugate().RotateVector(trans) / scale;
			samples_x[i] = trans.x;
			samples_y[i] = trans.y;
			samples_z[i] = trans.z;
			bIdentity = bIdentity && trans.IsEquiv(kIdentityTranslation, g_fEpsilonTranslation);
		}
		anim->SetSamples(ITSCENE::kAnimTrackTranslateX, samples_x);
		anim->SetSamples(ITSCENE::kAnimTrackTranslateY, samples_y);
		anim->SetSamples(ITSCENE::kAnimTrackTranslateZ, samples_z);
		bIdentity = false; // no, allow identity tracks
		if (bIdentity) {
			// Delete any additive identity tracks
			INOTE("Deleting identity joint anim '%s' from additive animation\n", anim->TargetPathString().c_str());
			animScene.m_jointAnimations.erase( animScene.m_jointAnimations.begin() + iJointAnim );
			numJointAnims = animScene.GetNumJointAnimations();
		} else {
			++iJointAnim;
		}
	}

	size_t numFloatAnims = animScene.GetNumFloatAttributeAnims();
	for (size_t iFloatAnim = 0; iFloatAnim < numFloatAnims; )
	{
		ITSCENE::FloatAttributeAnim *anim = animScene.m_floatAttributeAnims[iFloatAnim];
		ITSCENE::FloatAttributeAnim const *refFloatAnimation = NULL;
		std::string const& floatAnimName = anim->TargetPathString();

		// search the reference scene for the corresponding animation
		for (size_t animFloatIndex=0; animFloatIndex < refScene.GetNumFloatAttributeAnims(); animFloatIndex++) {
			if (floatAnimName == refScene.m_floatAttributeAnims[animFloatIndex]->TargetPathString()) {
				refFloatAnimation = refScene.m_floatAttributeAnims[animFloatIndex];
				break;
			}
		}

		if (!refFloatAnimation) {
			IWARN("Float animation '%s' has no correspoding float channel in bind pose scene - ignoring...\n", floatAnimName.c_str());
			animScene.m_floatAttributeAnims.erase( animScene.m_floatAttributeAnims.begin() + iFloatAnim );
			numFloatAnims = animScene.GetNumFloatAttributeAnims();
			continue;
		}

		bool bIdentity = true;

		// get samples from reference animation
		ITDATAMGR::FloatArray refsamples;
		refFloatAnimation->GetSamples( refsamples );

		// subtract ref samples from all samples of float channel
		size_t samplesN = anim->GetNumSamples();
		ITDATAMGR::FloatArray samples;
		anim->GetSamples(samples);
		for (size_t i=0; i<samplesN; ++i) {
			unsigned refFrame = useOnlyStartFrame ? startFrame : startFrame + (unsigned)i;
			if (refFrame < 0) {
				refFrame = (isRefLooping) ? (unsigned)refsamples.size() - (((unsigned)refsamples.size()-refFrame) % (unsigned)refsamples.size()) : 0;
			} else if (refFrame >= (unsigned)refsamples.size()) {
				refFrame = (isRefLooping) ? (refFrame % (unsigned)refsamples.size()) : (unsigned)refsamples.size()-1;
			}
			samples[i] -= refsamples[refFrame];
			bIdentity = bIdentity && fabsf(samples[i]) <= g_fEpsilonFloat;
		}
		anim->SetSamples(samples);
		bIdentity = false; // no, allow identity tracks
		if (bIdentity) {
			// Delete any additive identity tracks
			INOTE("Deleting identity float attribute anim '%s' from additive animation\n", anim->TargetPathString().c_str());
			animScene.m_floatAttributeAnims.erase( animScene.m_floatAttributeAnims.begin() + iFloatAnim );
			numFloatAnims = animScene.GetNumFloatAttributeAnims();
		} else {
			++iFloatAnim;
		}
	}

	animScene.m_flags |= ITSCENE::BSF_ADDITIVE;
}

		}	//namespace AnimProcessing
	}	//namespace Tools
}	//namespace OrbisAnim
