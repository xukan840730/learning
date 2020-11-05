/*
 * Copyright (c) 2005 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "animmetadata.h"
#include "animcompression.h"
#include "animobjectrootanims.h"
#include "icelib/itscene/studiopolicy.h"
#include "icelib/itscene/itjointanim.h"
#include "icelib/itscene/itgeneralanim.h"

namespace OrbisAnim {
namespace Tools {
namespace AnimProcessing {

static const unsigned kPrepDataVersionMajor = 2;	// prep data version 2.00 - supports segments
static const unsigned kPrepDataVersionMinor = 1;	// support for floats in segments
static const unsigned kPrepDataVersionMajor_NoSegments = 1;	// prep data version 1.00
static const unsigned kPrepDataVersionMinor_NoSegments = 0;

bool ExtractClipDataIntermediateData(AnimationHierarchyDesc& hierarchyDesc,
									 AnimationBinding& binding,
									 AnimationClipProperties& clipProperties,
									 AnimationClipSourceData& sourceData,
									 ClipCompressionDesc& compression,
									 ITSCENE::SceneDb const& bindPoseScene,
									 OutputAnimDesc const& outputAnimDesc,
									 AnimMetaData const& metaData, 
									 AnimationClipCompressionStats* stats)
{
	bool bSuccess = ExtractClipDataIntermediateData(hierarchyDesc, binding, clipProperties, sourceData, bindPoseScene, outputAnimDesc);

	// parse metadata & create compression struct
	if (!AnimMetaDataIsUncompressed(metaData)) {
		BindAnimMetaDataToClip(compression, metaData, hierarchyDesc, binding, sourceData, stats);
	} else {
		AnimClipCompression_SetUncompressed(compression, metaData, hierarchyDesc, binding, sourceData, stats);
	}
	DumpAnimationClipCompression(compression, hierarchyDesc, binding, sourceData, outputAnimDesc.m_inputData.m_animScenePath);
	
	return bSuccess;
}

bool ExtractClipDataIntermediateData(AnimationHierarchyDesc& hierarchyDesc,
									 AnimationBinding& binding,
									 AnimationClipProperties& clipProperties,
									 AnimationClipSourceData& sourceData,
									 ITSCENE::SceneDb const& bindPoseScene,
									 OutputAnimDesc const& outputAnimDesc)
{
	if (outputAnimDesc.m_inputData.m_frameSet.m_endFrame == outputAnimDesc.m_inputData.m_frameSet.m_startFrame) {
		INOTE(IMsg::kBrief, "%s:%d\n", outputAnimDesc.m_inputData.m_animScenePath.c_str(), outputAnimDesc.m_inputData.m_frameSet.m_startFrame);
	} else if (outputAnimDesc.m_inputData.m_frameSet.m_endFrame != AnimFrameSet::kFrameEnd) {
		INOTE(IMsg::kBrief, "%s:%d-%d\n", outputAnimDesc.m_inputData.m_animScenePath.c_str(), outputAnimDesc.m_inputData.m_frameSet.m_startFrame, outputAnimDesc.m_inputData.m_frameSet.m_endFrame);
	} else if (outputAnimDesc.m_inputData.m_frameSet.m_startFrame != 0) {
		INOTE(IMsg::kBrief, "%s:%d-\n", outputAnimDesc.m_inputData.m_animScenePath.c_str(), outputAnimDesc.m_inputData.m_frameSet.m_startFrame);
	} else {
		INOTE(IMsg::kBrief, "%s\n", outputAnimDesc.m_inputData.m_animScenePath.c_str());
	}
	ITASSERT(outputAnimDesc.m_inputData.m_pAnimScene);

	// get hierarchy info from bind pose scene
	GetAnimationHierarchyDesc(hierarchyDesc, bindPoseScene);

	// fps etc
	CollectAnimationClipProperties(clipProperties, outputAnimDesc.m_inputData);

	// build binding info between anims in animScene & the hierarchy (animated & output joint indices)
	ITSCENE::SceneDb const& animScene = *outputAnimDesc.m_inputData.m_pAnimScene;
	BindAnimationScene(binding, hierarchyDesc, animScene);
	
	// collect per-component anim tracks into joint & float channels
	CollectAnimationClipSourceData(sourceData, outputAnimDesc.m_inputData, binding);
	
	if (clipProperties.m_objectRootAnim == kObjectRootAnimLinear) {
		ExtractObjectRootAnim(ExtractLinearRootAnim, sourceData, clipProperties, hierarchyDesc, binding);
	}

	// subtract animation if valid
	if (outputAnimDesc.m_subtractData.IsValid()) {
		AnimationBinding bindingSubtract;
		AnimationClipSourceData sourceDataSubtract;
		float fFrameRateSubtract = 0.0f;
		if (outputAnimDesc.m_subtractData.IsBindPose()) {
			INOTE(IMsg::kBrief, "  additive from bind pose\n");
			BuildIdentityBinding(bindingSubtract, hierarchyDesc);
			CollectAnimationClipSourceDataFromBindPose(sourceDataSubtract, hierarchyDesc, bindingSubtract);
		} else {
			if (outputAnimDesc.m_inputData.m_frameSet.m_endFrame != AnimFrameSet::kFrameEnd) {
				INOTE(IMsg::kBrief, "  additive from %s:%d-%d\n", outputAnimDesc.m_subtractData.m_animScenePath.c_str(), outputAnimDesc.m_subtractData.m_frameSet.m_startFrame, outputAnimDesc.m_subtractData.m_frameSet.m_endFrame);
			} else {
				INOTE(IMsg::kBrief, "  additive from %s:%d-\n", outputAnimDesc.m_subtractData.m_animScenePath.c_str(), outputAnimDesc.m_subtractData.m_frameSet.m_startFrame);
			}
			AnimationClipProperties clipPropertiesSubtract;
			CollectAnimationClipProperties(clipPropertiesSubtract, outputAnimDesc.m_subtractData);
			fFrameRateSubtract = clipPropertiesSubtract.m_framesPerSecond;
			ITSCENE::SceneDb const& animSceneSubtract = *outputAnimDesc.m_subtractData.m_pAnimScene;
			BindAnimationScene(bindingSubtract, hierarchyDesc, animSceneSubtract);
			CollectAnimationClipSourceData(sourceDataSubtract, outputAnimDesc.m_subtractData, bindingSubtract);
			if (clipPropertiesSubtract.m_objectRootAnim == kObjectRootAnimLinear) {
				ExtractObjectRootAnim(ExtractLinearRootAnim, sourceDataSubtract, clipPropertiesSubtract, hierarchyDesc, bindingSubtract);
			}
		}
		float fSubtractFrameStep = outputAnimDesc.m_fSubtractFrameStep;
		if (fSubtractFrameStep == 0.0f && clipProperties.m_framesPerSecond > 0.0f && sourceDataSubtract.m_numFrames > 1) {
			fSubtractFrameStep = outputAnimDesc.m_fSubtractFrameSpeed * fFrameRateSubtract / clipProperties.m_framesPerSecond;
		}
		SubtractAnimation(sourceData, binding, sourceData, binding, sourceDataSubtract, bindingSubtract, outputAnimDesc.m_fSubtractFrameOffset, fSubtractFrameStep);
	}

	return true;
}

unsigned CollectJoints(std::vector<Joint> &joints, std::vector<ITSCENE::Joint*> const& sceneJoints, std::set<size_t> const& animatedJointSet)
{
	unsigned const numJoints = (unsigned)sceneJoints.size();
	joints.resize(numJoints);
	auto it = sceneJoints.begin(), itEnd = sceneJoints.end();
	for (unsigned iJoint = 0; it != itEnd; ++it, ++iJoint) {
		ITSCENE::Joint const* pSceneJoint = *it;
		Joint& joint = joints[iJoint];
		joint.m_name = pSceneJoint->m_name;
		joint.m_flags = 0;
		if (pSceneJoint->m_segmentScaleCompensation) {
			joint.m_flags |= kJointSegmentScaleCompensate;
		}
		if (animatedJointSet.count(iJoint)) {
			joint.m_flags |= kJointIsAnimated;
		}
		joint.m_jointScale = pSceneJoint->m_jointScale;
		ITGEOM::QuatFromMayaEulerAngles(&joint.m_jointQuat, pSceneJoint->m_jointRotate, pSceneJoint->m_rotateAxis, pSceneJoint->m_jointOrient, pSceneJoint->m_rotateOrder );
		joint.m_jointTranslation = pSceneJoint->m_transformMatrix[3];

		int iParent = ITSCENE::GetParentJoint(iJoint, sceneJoints);
		joint.m_parent = iParent;
		joint.m_child = -1;
		joint.m_sibling = -1;

		// collect euler parameters of joint (may be used by anim curves)
		static const Joint::EulerOrder toOrbisAnimEulerOrder[] = {
			Joint::kRoXYZ,	//kEOXyz,
			Joint::kRoYZX,	//kEOYzx,
			Joint::kRoZXY,	//kEOZxy,
			Joint::kRoXZY,	//kEOXzy,
			Joint::kRoYXZ,	//kEOYxz,
			Joint::kRoZYX,	//kEOZyx
		};
		joint.m_eulerOrder = toOrbisAnimEulerOrder[pSceneJoint->m_rotateOrder];
		ITGEOM::GetRotateQuat(&joint.m_jointOrient, pSceneJoint->m_jointOrient);
		ITGEOM::GetRotateQuat(&joint.m_rotAxis, pSceneJoint->m_rotateAxis);
	}

	// set up child and sibling indices, now that we've collected all parent indices
	int iPrevRoot = -1;
	for (unsigned iJoint = 0; iJoint < numJoints; ++iJoint) {
		int iParent = joints[iJoint].m_parent;
		if (iParent == -1) {
			if (iPrevRoot != -1) {
				joints[iPrevRoot].m_sibling = iJoint;
			}
			iPrevRoot = iJoint;
		} else {
			int iParentChild = joints[iParent].m_child;
			if (iParentChild == -1) {
				joints[iParent].m_child = iJoint;
			} else {
				int iParentLastChild = iParentChild;
				while (joints[iParentLastChild].m_sibling != -1) {
					iParentLastChild = joints[iParentLastChild].m_sibling;
				}
				joints[iParentLastChild].m_sibling = iJoint;
			}
		}
	}
	return numJoints;
}

unsigned CollectFloatChannels(std::vector<FloatChannel> &floatChannels, std::vector<ITSCENE::FloatAttribute*> const& floatAttributes)
{
	std::vector<ITSCENE::FloatAttribute*>::const_iterator it = floatAttributes.begin(), itEnd = floatAttributes.end();
	for (; it != itEnd; ++it) {
		ITSCENE::FloatAttribute const* pFa = *it;
		if (pFa->IsType(ITSCENE::kFaAnimOutput)) {
			FloatChannel floatChannel;
			floatChannel.m_name = pFa->GetFullName();
			floatChannel.m_defaultValue = pFa->GetDefaultValue();
			floatChannels.push_back( floatChannel );
		}
	}
	return (unsigned)floatChannels.size();
}

int FindJointIndexByName(std::vector<Joint> const& joints, std::string const& name)
{
	std::vector<Joint>::const_iterator it = joints.begin(), itEnd = joints.end();
	for (int iJoint = 0; it != itEnd; ++it, ++iJoint)
		if (it->m_name == name)
			return iJoint;
	return -1;
}

int FindJointIndexByNameIgnoreNamespace(std::vector<Joint> const& joints, std::string const& name)
{
	std::vector<Joint>::const_iterator it = joints.begin(), itEnd = joints.end();
	for (int iJoint = 0; it != itEnd; ++it, ++iJoint)
		if (ITSCENE::StripNamespace(it->m_name) == ITSCENE::StripNamespace(name))
			return iJoint;
	return -1;
}

int FindFloatChannelIndexByName(std::vector<FloatChannel> const& floatChannels, std::string const& name)
{
	std::vector<FloatChannel>::const_iterator it = floatChannels.begin(), itEnd = floatChannels.end();
	for (int iFloatChannel = 0; it != itEnd; ++it, ++iFloatChannel)
		if (it->m_name == name)
			return iFloatChannel;
	return -1;
}

int FindFloatChannelIndexByNameIgnoreNamespace(std::vector<FloatChannel> const& floatChannels, std::string const& name)
{
	std::vector<FloatChannel>::const_iterator it = floatChannels.begin(), itEnd = floatChannels.end();
	for (int iFloatChannel = 0; it != itEnd; ++it, ++iFloatChannel)
		if (ITSCENE::StripNamespace(it->m_name) == ITSCENE::StripNamespace(name))
			return iFloatChannel;
	return -1;
}

bool GetAnimationHierarchyDesc(AnimationHierarchyDesc& hierarchyDesc, ITSCENE::SceneDb const& preppedBindPoseScene)
{
	std::string strValue;
	unsigned iVersionMajor, iVersionMinor;
	if (!preppedBindPoseScene.GetInfo(&strValue, "prepDataVersion")) {
		IERR("prepData sceneinfo 'prepDataVersion' not found - scene not prepped or pre-1.00 version\n");
		return false;
	}
	if (2 != sscanf(strValue.c_str(), "%u.%02u", &iVersionMajor, &iVersionMinor)) {
		IERR("prepData sceneinfo 'prepDataVersion' has invalid value '%s'\n", strValue.c_str());
		return false;
	}
	bool bIsVersionSegmented = true;
	if (iVersionMajor != kPrepDataVersionMajor || iVersionMinor != kPrepDataVersionMinor) {
		if (iVersionMajor != kPrepDataVersionMajor_NoSegments || iVersionMinor != kPrepDataVersionMinor_NoSegments) {
			IERR("prepData sceneinfo 'prepDataVersion' from version %u.%02u != expected %u.%02u\n", iVersionMajor, iVersionMinor, kPrepDataVersionMajor, kPrepDataVersionMinor);
			return false;
		}
		bIsVersionSegmented = false;
	}
	if (!preppedBindPoseScene.GetInfo(&strValue, "numAnimatedJoints")) {
		IERR("prepData version %u.%02u sceneinfo 'numAnimatedJoints' not found\n", iVersionMajor, iVersionMinor);
		return false;
	}
	hierarchyDesc.m_numAnimatedJoints = atoi(strValue.c_str());
	if (!preppedBindPoseScene.GetInfo(&strValue, "numFloatChannels")) {
		IERR("prepData version %u.%02u sceneinfo 'numFloatChannels' not found\n", iVersionMajor, iVersionMinor);
		return false;
	}
	hierarchyDesc.m_numFloatChannels = atoi(strValue.c_str());
	if (!preppedBindPoseScene.GetInfo(&strValue, "hierarchyId")) {
		IERR("prepData version %u.%02u sceneinfo 'hierarchyId' not found\n", iVersionMajor, iVersionMinor);
		return false;
	}
	if (!sscanf(strValue.c_str(), "%08x", &hierarchyDesc.m_hierarchyId)) {
		IERR("prepData version %u.%02u sceneinfo 'hierarchyId' has invalid value '%s'\n", iVersionMajor, iVersionMinor, strValue.c_str());
		return false;
	}
	hierarchyDesc.m_channelGroups.clear();
	if (!preppedBindPoseScene.GetInfo(&strValue, "numProcessingGroups")) {
		IERR("prepData version %u.%02u sceneinfo 'numProcessingGroups' not found\n", iVersionMajor, iVersionMinor);
		return false;
	}
	unsigned numGroups = atoi(strValue.c_str());
	hierarchyDesc.m_channelGroups.resize(numGroups);
	unsigned numAnimatedJoints = 0, numFloatChannels = 0;
	for (unsigned iGroup = 0; iGroup < numGroups; ++iGroup) {
		char namebuf[100];
		char* pAttrib = namebuf + sprintf(namebuf, (bIsVersionSegmented ? "processingGroup[%u]." : "group[%u]."), iGroup);
		sprintf(pAttrib, (bIsVersionSegmented ? "numAnimatedJoints" : "numJoints"));
		if (!preppedBindPoseScene.GetInfo(&strValue, namebuf)) {
			IERR("prepData version %u.%02u sceneinfo '%s' not found\n", iVersionMajor, iVersionMinor, namebuf);
			return false;
		}
		unsigned numAnimatedJointsInGroup = atoi(strValue.c_str());
		sprintf(pAttrib, "numFloatChannels");
		if (!preppedBindPoseScene.GetInfo(&strValue, namebuf)) {
			IERR("prepData version %u.%02u sceneinfo '%s' not found\n", iVersionMajor, iVersionMinor, namebuf);
			return false;
		}
		unsigned numFloatChannelsInGroup = atoi(strValue.c_str());
		if (numAnimatedJointsInGroup > kJointGroupSize || numFloatChannelsInGroup > kFloatChannelGroupSize) {
			ITASSERT(numAnimatedJointsInGroup <= kJointGroupSize && numFloatChannelsInGroup <= kFloatChannelGroupSize);
			return false;
		}
		hierarchyDesc.m_channelGroups[iGroup].m_numAnimatedJoints = numAnimatedJointsInGroup;
		hierarchyDesc.m_channelGroups[iGroup].m_numFloatChannels = numFloatChannelsInGroup;
		hierarchyDesc.m_channelGroups[iGroup].m_firstAnimatedJoint = numAnimatedJoints;
		hierarchyDesc.m_channelGroups[iGroup].m_firstFloatChannel = numFloatChannels;
		numAnimatedJoints += numAnimatedJointsInGroup;
		numFloatChannels += numFloatChannelsInGroup;
	}
	if (numAnimatedJoints != hierarchyDesc.m_numAnimatedJoints) {
		IERR("prepData version %u.%02u sceneinfo 'numAnimatedJoints' value %u does not match group total %u\n", iVersionMajor, iVersionMinor, hierarchyDesc.m_numAnimatedJoints, numAnimatedJoints);
		return false;
	}
	if (numFloatChannels != hierarchyDesc.m_numFloatChannels) {
		IERR("prepData version %u.%02u sceneinfo 'numFloatChannels' value %u does not match group total %u\n", iVersionMajor, iVersionMinor, hierarchyDesc.m_numFloatChannels, numFloatChannels);
		return false;
	}

	std::set<size_t> animatedJointSet;
	hierarchyDesc.m_animatedJointRanges.clear();
	if (iVersionMajor != kPrepDataVersionMajor_NoSegments || iVersionMinor != kPrepDataVersionMinor_NoSegments) {
		if (!preppedBindPoseScene.GetInfo(&strValue, "numJoints")) {
			IERR("prepData version %u.%02u sceneinfo 'numJoints' not found\n", iVersionMajor, iVersionMinor);
			return false;
		}
		if (!preppedBindPoseScene.GetInfo(&strValue, "numAnimatedJointRanges")) {
			IERR("prepData version %u.%02u sceneinfo 'numAnimatedJointRanges' not found\n", iVersionMajor, iVersionMinor);
			return false;
		}
		unsigned const numAnimatedJointRanges = atoi(strValue.c_str());
		hierarchyDesc.m_animatedJointRanges.resize(numAnimatedJointRanges);
		unsigned numAnimatedJointsInRanges = 0;
		for (unsigned i = 0; i < numAnimatedJointRanges; ++i) {
			char namebuf[100];
			sprintf(namebuf, "animatedJointRange[%u].numJoints", i);
			if (!preppedBindPoseScene.GetInfo(&strValue, namebuf)) {
				IERR("prepData version %u.%02u sceneinfo '%s' not found\n", iVersionMajor, iVersionMinor, namebuf);
				return false;
			}
			unsigned numJointsInRange = atoi(strValue.c_str());
			sprintf(namebuf, "animatedJointRange[%u].firstJoint", i);
			if (!preppedBindPoseScene.GetInfo(&strValue, namebuf)) {
				IERR("prepData version %u.%02u sceneinfo '%s' not found\n", iVersionMajor, iVersionMinor, namebuf);
				return false;
			}
			unsigned firstJointInRange = atoi(strValue.c_str());
			hierarchyDesc.m_animatedJointRanges[i].m_firstJoint = firstJointInRange;
			hierarchyDesc.m_animatedJointRanges[i].m_numJoints = numJointsInRange;
			for (size_t iJoint = 0; iJoint < numJointsInRange; iJoint++) {
				animatedJointSet.emplace(firstJointInRange + iJoint);
			}
			numAnimatedJointsInRanges += numJointsInRange;
		}
		if (numAnimatedJointsInRanges != hierarchyDesc.m_numAnimatedJoints) {
			IERR("prepData version %u.%02u sceneinfo 'numAnimatedJoints' value %u does not match animatedJointRange total %u\n", iVersionMajor, iVersionMinor, hierarchyDesc.m_numAnimatedJoints, numAnimatedJointsInRanges);
			return false;
		}
	} else {
		hierarchyDesc.m_animatedJointRanges.resize(1);
		hierarchyDesc.m_animatedJointRanges[0].m_firstJoint = 0;
		hierarchyDesc.m_animatedJointRanges[0].m_numJoints = numAnimatedJoints;
		for (size_t iJoint = 0; iJoint < numAnimatedJoints; iJoint++) {
			animatedJointSet.emplace(iJoint);
		}
	}

	CollectJoints(hierarchyDesc.m_joints, preppedBindPoseScene.m_joints, animatedJointSet);
	unsigned numFloatChannels_Attributes = CollectFloatChannels(hierarchyDesc.m_floatChannels, preppedBindPoseScene.m_floatAttributes);
	ITASSERT( numFloatChannels_Attributes == hierarchyDesc.m_numFloatChannels );
	hierarchyDesc.m_objectRootJoint = g_theStudioPolicy->GetObjectRootJoint(preppedBindPoseScene);
	return true;
}

int AnimatedJointIndexToJointId(	AnimationHierarchyDesc const& hierarchyDesc,
									unsigned iAnimatedJointIndex )
{
	unsigned iRangeRelativeIndex = iAnimatedJointIndex;
	for (size_t iRange = 0, numRanges = hierarchyDesc.m_animatedJointRanges.size(); iRange < numRanges; ++iRange) {
		AnimatedJointRange const& animatedJointRange = hierarchyDesc.m_animatedJointRanges[iRange];
		if (iRangeRelativeIndex < animatedJointRange.m_numJoints)
			return animatedJointRange.m_firstJoint + iRangeRelativeIndex;
		iRangeRelativeIndex -= animatedJointRange.m_numJoints;
	}
	return -1;
}

int JointIdToAnimatedJointIndex(	AnimationHierarchyDesc const& hierarchyDesc,
									unsigned iJoint )
{
	unsigned iAnimatedJointIndex = 0;
	for (size_t iRange = 0, numRanges = hierarchyDesc.m_animatedJointRanges.size(); iRange < numRanges; ++iRange) {
		AnimatedJointRange const& animatedJointRange = hierarchyDesc.m_animatedJointRanges[iRange];
		if (iJoint >= animatedJointRange.m_firstJoint && iJoint < animatedJointRange.m_firstJoint + animatedJointRange.m_numJoints)
			return iAnimatedJointIndex + iJoint - animatedJointRange.m_firstJoint;
		iAnimatedJointIndex += animatedJointRange.m_numJoints;
	}
	return -1;
}

void BuildIdentityBinding(AnimationBinding& binding, AnimationHierarchyDesc const& hierarchyDesc)
{
	unsigned const numJoints = (unsigned)hierarchyDesc.m_joints.size();
	binding.m_hierarchyId = hierarchyDesc.m_hierarchyId;
	binding.m_animatedJointIndexToJointAnimId.resize(hierarchyDesc.m_numAnimatedJoints);
	binding.m_jointAnimIdToAnimatedJointIndex.resize(hierarchyDesc.m_numAnimatedJoints);
	binding.m_jointAnimIdToJointId.resize(hierarchyDesc.m_numAnimatedJoints);
	binding.m_jointIdToJointAnimId.resize(numJoints);
	for (unsigned iJoint = 0; iJoint < hierarchyDesc.m_numAnimatedJoints; ++iJoint) {
		binding.m_animatedJointIndexToJointAnimId[iJoint] = iJoint;
		binding.m_jointAnimIdToAnimatedJointIndex[iJoint] = iJoint;
		binding.m_jointAnimIdToJointId[iJoint] = iJoint;
		binding.m_jointIdToJointAnimId[iJoint] = iJoint;
	}
	for (unsigned iJoint = hierarchyDesc.m_numAnimatedJoints; iJoint < numJoints; ++iJoint) {
		binding.m_jointIdToJointAnimId[iJoint] = -1;
	}
	binding.m_floatAnimIdToFloatId.resize(hierarchyDesc.m_numFloatChannels);
	binding.m_floatIdToFloatAnimId.resize(hierarchyDesc.m_numFloatChannels);
	for (unsigned iFloat = 0; iFloat < hierarchyDesc.m_numFloatChannels; ++iFloat) {
		binding.m_floatAnimIdToFloatId[iFloat] = iFloat;
		binding.m_floatIdToFloatAnimId[iFloat] = iFloat;
	}
}

std::string UnmangleAttributeName(std::string const& fullDagPath, ITSCENE::SceneDb const& animScene)
{
	std::string::size_type posDot = fullDagPath.find_first_of('.');
	if (posDot == std::string::npos) {
		return g_theStudioPolicy->UnmangleName(fullDagPath, animScene);
	}
	std::string nodeName = fullDagPath.substr(0, posDot);
	std::string dotAttrName = fullDagPath.substr(posDot);
	return g_theStudioPolicy->UnmangleName(nodeName, animScene) + dotAttrName;
}

void BindAnimationScene(AnimationBinding& binding, AnimationHierarchyDesc const& hierarchyDesc, ITSCENE::SceneDb const& animScene)
{
	binding.m_hierarchyId = hierarchyDesc.m_hierarchyId;
	binding.m_jointAnimIdToAnimatedJointIndex.clear();
	binding.m_animatedJointIndexToJointAnimId.clear();
	binding.m_jointAnimIdToJointId.clear();
	binding.m_jointIdToJointAnimId.clear();
	binding.m_floatAnimIdToFloatId.clear();
	binding.m_floatIdToFloatAnimId.clear();

	unsigned const numJoints = (unsigned)hierarchyDesc.m_joints.size();
	binding.m_jointAnimIdToAnimatedJointIndex.resize(animScene.GetNumJointAnimations(), -1);
	binding.m_animatedJointIndexToJointAnimId.resize(hierarchyDesc.m_numAnimatedJoints, -1);
	binding.m_jointAnimIdToJointId.resize(animScene.GetNumJointAnimations(), -1);
	binding.m_jointIdToJointAnimId.resize(numJoints, -1);
    for (unsigned iJointAnimIndex = 0; iJointAnimIndex < animScene.GetNumJointAnimations(); iJointAnimIndex++) {
		ITSCENE::JointAnimation const *anim = animScene.m_jointAnimations[iJointAnimIndex];
		if (!anim->HasSamples())
			continue;
		std::string jointName = g_theStudioPolicy->UnmangleName(anim->TargetPathString(), animScene);
		int	iJoint = FindJointIndexByNameIgnoreNamespace(hierarchyDesc.m_joints, jointName);
		if ((unsigned)iJoint >= numJoints) {
			INOTE(IMsg::kVerbose, "ICE> Animation of joint (%s) not in bindpose scene will be ignored.\n", anim->TargetPathString().c_str());
			continue;
		} else if (binding.m_jointIdToJointAnimId[iJoint] != -1) {

			ITASSERTF(binding.m_jointIdToJointAnimId[iJoint] == -1, ("ICE> ERROR: Joint '%s' has multiple matching animations '%s' and '%s'",

				jointName.c_str(), animScene.m_jointAnimations[binding.m_jointIdToJointAnimId[iJoint]]->TargetPathString().c_str(),  anim->TargetPathString().c_str()));

			continue;

		}
		int iAnimatedJoint = JointIdToAnimatedJointIndex(hierarchyDesc, (unsigned)iJoint);
		if (iAnimatedJoint < 0) {
			INOTE(IMsg::kVerbose, "ICE> Animation of procedural joint[%u] (%s) will be ignored.\n", iJoint, anim->TargetPathString().c_str());
			continue;
		}
		binding.m_jointIdToJointAnimId[iJoint] = iJointAnimIndex;
		binding.m_jointAnimIdToJointId[iJointAnimIndex] = iJoint;
		binding.m_animatedJointIndexToJointAnimId[iAnimatedJoint] = iJointAnimIndex;
		binding.m_jointAnimIdToAnimatedJointIndex[iJointAnimIndex] = iAnimatedJoint;
    }

	binding.m_floatAnimIdToFloatId.resize(animScene.GetNumFloatAttributeAnims(), -1);
	binding.m_floatIdToFloatAnimId.resize(hierarchyDesc.m_numFloatChannels, -1);
    for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < animScene.GetNumFloatAttributeAnims(); iFloatAnimIndex++) {
		ITSCENE::FloatAttributeAnim const *anim = animScene.m_floatAttributeAnims[iFloatAnimIndex];
		if (!anim->HasSamples())
			continue;
		std::string floatChannelName = UnmangleAttributeName(anim->TargetPathString(), animScene);
		int	iFloatChannel = FindFloatChannelIndexByNameIgnoreNamespace(hierarchyDesc.m_floatChannels, floatChannelName);
		if (iFloatChannel < 0) {
			INOTE(IMsg::kVerbose, "ICE> Animation of float attribute (%s) not in the bindpose scene will be ignored.\n", anim->TargetPathString().c_str());
			continue;
		} else if (binding.m_floatIdToFloatAnimId[iFloatChannel] != -1) {

			ITASSERTF(binding.m_floatIdToFloatAnimId[iFloatChannel] == -1, ("ICE> ERROR: FloatChannel '%s' has multiple matching animations '%s' and '%s'",

				floatChannelName.c_str(), animScene.m_floatAttributeAnims[binding.m_floatIdToFloatAnimId[iFloatChannel]]->TargetPathString().c_str(), anim->TargetPathString().c_str()));

			continue;

		}
		binding.m_floatIdToFloatAnimId[iFloatChannel] = iFloatAnimIndex;
		binding.m_floatAnimIdToFloatId[iFloatAnimIndex] = iFloatChannel;
    }
}

void CollectAnimationClipProperties(AnimationClipProperties& clipProperties, AnimDataSource const& animDataSource)
{
	switch (animDataSource.m_frameSet.m_eObjectRootAnim)
	{
	case AnimFrameSet::kObjectRootAnimNone:
		clipProperties.m_objectRootAnim = kObjectRootAnimNone;
		break;

	case AnimFrameSet::kObjectRootAnimLinear:
		clipProperties.m_objectRootAnim = kObjectRootAnimLinear;
		break;

	case AnimFrameSet::kObjectRootAnimDefault:
	default:
		clipProperties.m_objectRootAnim = kObjectRootAnimNone;
		break;
	}

	// use fps from scene info if it's valid
	std::string value;
	if (animDataSource.m_pAnimScene->GetInfo( &value, "fps" ) && value != "" && value != "0") {
		clipProperties.m_framesPerSecond = (float)atof(value.c_str());
		return;
	}

	// otherwise try to determine it from the duration if not set in source data
	clipProperties.m_framesPerSecond = animDataSource.m_frameSet.m_fFrameRate;
	if (clipProperties.m_framesPerSecond <= 0.0f && animDataSource.m_pAnimScene != NULL) {
		unsigned numSourceFrames = 0;
		float fDuration = 0.0f;
		for (unsigned iJointAnimIndex = 0; iJointAnimIndex < animDataSource.m_pAnimScene->GetNumJointAnimations(); iJointAnimIndex++) {
			ITSCENE::JointAnimation const *anim = animDataSource.m_pAnimScene->m_jointAnimations[iJointAnimIndex];
			if (!anim->HasSamples())
				continue;
			unsigned numSamples = (unsigned)-1;
			for (int track = 0; track < ITSCENE::kAnimTrackScaleZ; ++track) {
				unsigned numSamplesInTrack = (unsigned)anim->GetNumSamplesInTrack(track);
				if (numSamples == (unsigned)-1) {
					numSamples = numSamplesInTrack;
				} else {
					ITASSERTF(numSamplesInTrack == numSamples, ("ICE> ERROR: Joint animation (%s) track[%d] sample count %d does not match %d!", anim->TargetPathString().c_str(), track, numSamplesInTrack, numSamples));
				}
			}
			if (!numSourceFrames) {
				numSourceFrames = numSamples;
				fDuration = anim->m_duration;
			} else {
				ITASSERTF(numSourceFrames == numSamples, ("ICE> ERROR: Joint animation (%s) sample count %d does not match %d!", anim->TargetPathString().c_str(), numSamples, numSourceFrames));
				ITASSERT(anim->m_duration == fDuration);
			}
		}
		for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < animDataSource.m_pAnimScene->GetNumFloatAttributeAnims(); iFloatAnimIndex++) {
			ITSCENE::FloatAttributeAnim const *anim = animDataSource.m_pAnimScene->m_floatAttributeAnims[iFloatAnimIndex];
			if (!anim->HasSamples())
				continue;
			unsigned numSamples = (unsigned)anim->GetNumSamples();
			if (!numSourceFrames) {
				numSourceFrames = numSamples;
				fDuration = anim->m_duration;
			} else if (numSourceFrames != numSamples) {
				ITASSERTF(numSourceFrames == numSamples, ("ICE> ERROR: Float animation (%s) sample count %d does not match %d!", anim->TargetPathString().c_str(), numSamples, numSourceFrames));
				ITASSERT(anim->m_duration == fDuration);
			}
		}
		if (fDuration > 0.0f) {
			clipProperties.m_framesPerSecond = ((float)numSourceFrames / fDuration);
			// round to the nearest integer value of framesPerSecond, knowing that Maya framesPerSecond is always an integer...
			if (fabsf(clipProperties.m_framesPerSecond - (float)(int)(clipProperties.m_framesPerSecond + 0.5f)) > 0.0001f) {
				IWARN("CollectAnimationClipProperties: Animation '%s' has %d frames of data and duration %fs -> non-integral FPS %.2f!\n", animDataSource.m_animScenePath.c_str(), numSourceFrames, fDuration, clipProperties.m_framesPerSecond);
			}
			clipProperties.m_framesPerSecond = (float)(int)(clipProperties.m_framesPerSecond + 0.5f);
		}
	}
}

void CollectAnimationClipProperties(AnimationClipProperties& clipProperties, ITSCENE::SceneDb const& animScene)
{
	clipProperties.m_objectRootAnim = kObjectRootAnimNone;
	clipProperties.m_framesPerSecond = 0.0f;


	// use fps from scene info if it's valid
	std::string strValue;
	if (animScene.GetInfo( &strValue, "fps" ) && strValue != "" && strValue != "0") {
		clipProperties.m_framesPerSecond = (float)atof(strValue.c_str());
		return;
	}

	unsigned numSourceFrames = 0;
	float fDuration = 0.0f;
    for (unsigned iJointAnimIndex = 0; iJointAnimIndex < animScene.GetNumJointAnimations(); iJointAnimIndex++) {
		ITSCENE::JointAnimation const *anim = animScene.m_jointAnimations[iJointAnimIndex];
		if (!anim->HasSamples())
			continue;
		unsigned numSamples = (unsigned)-1;
		for (int track = 0; track < ITSCENE::kAnimTrackScaleZ; ++track) {
			unsigned numSamplesInTrack = (unsigned)anim->GetNumSamplesInTrack(track);
			if (numSamples == (unsigned)-1) {
				numSamples = numSamplesInTrack;
			} else {
				ITASSERTF(numSamplesInTrack == numSamples, ("ICE> ERROR: Joint animation (%s) track[%d] sample count %d does not match %d!", anim->TargetPathString().c_str(), track, numSamplesInTrack, numSamples));
			}
		}
		if (!numSourceFrames) {
			numSourceFrames = numSamples;
			fDuration = anim->m_duration;
		} else {
			ITASSERTF(numSourceFrames == numSamples, ("ICE> ERROR: Joint animation (%s) sample count %d does not match %d!", anim->TargetPathString().c_str(), numSamples, numSourceFrames));
			ITASSERT(anim->m_duration == fDuration);
		}
	}
    for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < animScene.GetNumFloatAttributeAnims(); iFloatAnimIndex++) {
		ITSCENE::FloatAttributeAnim const *anim = animScene.m_floatAttributeAnims[iFloatAnimIndex];
		if (!anim->HasSamples())
			continue;
		unsigned numSamples = (unsigned)anim->GetNumSamples();
		if (!numSourceFrames) {
			numSourceFrames = numSamples;
			fDuration = anim->m_duration;
		} else if (numSourceFrames != numSamples) {
			ITASSERTF(numSourceFrames == numSamples, ("ICE> ERROR: Float animation (%s) sample count %d does not match %d!", anim->TargetPathString().c_str(), numSamples, numSourceFrames));
			ITASSERT(anim->m_duration == fDuration);
		}
	}

	if (fDuration > 0.0f) {
		clipProperties.m_framesPerSecond = ((float)numSourceFrames / fDuration);
		// round to the nearest integer value of framesPerSecond, knowing that Maya framesPerSecond is always an integer...
		clipProperties.m_framesPerSecond = (float)(int)(clipProperties.m_framesPerSecond + 0.5f);
	}
}

void BindAnimMetaDataConstantErrorTolerancesToClip(ClipLocalSpaceErrors& tolerances,
												   AnimMetaData const& metaData,
												   AnimationHierarchyDesc const& hierarchyDesc,
												   AnimationBinding const& binding)
{
	ITASSERT( binding.m_hierarchyId == hierarchyDesc.m_hierarchyId );

	unsigned const numJoints = (unsigned)binding.m_jointIdToJointAnimId.size();
	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToJointId.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();

	// get const error tolerances defined in metadata
	float defaultScaleTolerance = metaData.m_defaultCompression.m_scale.m_format.m_fConstErrorTolerance;
	float defaultRotationTolerance = metaData.m_defaultCompression.m_rotation.m_format.m_fConstErrorTolerance;
	float defaultTranslationTolerance = metaData.m_defaultCompression.m_translation.m_format.m_fConstErrorTolerance;
	float defaultFloatTolerance = metaData.m_defaultCompressionFloat.m_format.m_fConstErrorTolerance;
	
	// use defaults if requested
	if (defaultScaleTolerance == kfConstErrorToleranceUseDefault) {
		defaultScaleTolerance = GetScaleErrorTolerance();
	}
	if (defaultRotationTolerance == kfConstErrorToleranceUseDefault) {
		defaultRotationTolerance = GetRotationErrorTolerance();
	}
	if (defaultTranslationTolerance == kfConstErrorToleranceUseDefault) {
		defaultTranslationTolerance = GetTranslationErrorTolerance();
	}
	if (defaultFloatTolerance == kfConstErrorToleranceUseDefault) {
		defaultFloatTolerance = GetFloatChannelErrorTolerance();
	}

	// broadcast to all channels
	tolerances.m_const[kChannelTypeScale].assign(numJointAnims, defaultScaleTolerance);
	tolerances.m_const[kChannelTypeRotation].assign(numJointAnims, defaultRotationTolerance);
	tolerances.m_const[kChannelTypeTranslation].assign(numJointAnims, defaultTranslationTolerance);
	tolerances.m_const[kChannelTypeScalar].assign(numFloatAnims, defaultFloatTolerance);

	// check for overrides and auto tolerances on joint channels
	for (unsigned iJointAnim = 0; iJointAnim < numJointAnims; ++iJointAnim) {
		unsigned iJoint = (unsigned)binding.m_jointAnimIdToJointId[iJointAnim];
		if (iJoint >= numJoints) {	// anim not bound to a joint
			continue;
		}
		// override defaults if named in metadata
		int iMeta = FindAnimMetaDataIndexForName( hierarchyDesc.m_joints[iJoint].m_name, metaData.m_jointNames );
		if (iMeta >= 0) {
			AnimMetaDataJointCompressionMethod const& method = metaData.m_jointCompression[iMeta];
			float scaleTolerance = method.m_scale.m_format.m_fConstErrorTolerance;
			if (scaleTolerance != kfConstErrorToleranceUseDefault) {
				tolerances.m_const[kChannelTypeScale][iJointAnim] = scaleTolerance;
			}
			float rotationTolerance = method.m_rotation.m_format.m_fConstErrorTolerance;
			if (rotationTolerance != kfConstErrorToleranceUseDefault) {
				tolerances.m_const[kChannelTypeRotation][iJointAnim] = rotationTolerance;
			}
			float translationTolerance = method.m_translation.m_format.m_fConstErrorTolerance;
			if (translationTolerance != kfConstErrorToleranceUseDefault) {
				tolerances.m_const[kChannelTypeTranslation][iJointAnim] = translationTolerance;
			}
		} 
		// if auto, use the bit compression tolerances
		for (size_t i=0; i<kNumJointChannels; i++) {
			if (tolerances.m_const[i][iJointAnim] == kConstErrorToleranceAuto) {
				tolerances.m_const[i][iJointAnim] = tolerances.m_bit[i][iJointAnim];
			}
		}
	}

	// check for overrides and auto tolerances on float channels
	for (unsigned iFloatAnim = 0; iFloatAnim < numFloatAnims; ++iFloatAnim) {
		unsigned iFloatChannel = (unsigned)binding.m_floatAnimIdToFloatId[iFloatAnim];
		if (iFloatChannel >= hierarchyDesc.m_numFloatChannels) {	// anim not bound to a float
			continue;
		}
		// override defaults if named in metadata
		int i = FindAnimMetaDataIndexForName( hierarchyDesc.m_floatChannels[iFloatChannel].m_name, metaData.m_floatNames );
		if (i >= 0) {
			AnimMetaDataTrackCompressionMethod const& floatCompression = metaData.m_floatCompression[i];
			float floatTolerance = floatCompression.m_format.m_fConstErrorTolerance;
			if (floatTolerance != kfConstErrorToleranceUseDefault) {
				tolerances.m_const[kChannelTypeScalar][iFloatAnim] = floatTolerance;
			}
		}
		// if auto, use the bit compression tolerance
		if (tolerances.m_const[kChannelTypeScalar][iFloatAnim] == kConstErrorToleranceAuto) {
			tolerances.m_const[kChannelTypeScalar][iFloatAnim] = tolerances.m_bit[kChannelTypeScalar][iFloatAnim];
		}
	}
}

static ITSCENE::GeneralCurveConnection const* GetGeneralCurveConnection(ITSCENE::SceneDb const& scene, std::string const& nodeName, std::string const& attrName)
{
	for (unsigned iGeneralConn = 0; iGeneralConn < scene.m_generalCurveConnections.size(); iGeneralConn++) {
		ITSCENE::GeneralCurveConnection const* generalConnect = scene.m_generalCurveConnections[iGeneralConn];
		if (generalConnect) {
			if (generalConnect->m_destinationNode == nodeName) {
				if (generalConnect->m_destinationAttribute == attrName) {
					return generalConnect;
				}
			}
		}
	}
	return NULL;
}

static ITSCENE::GeneralAnimCurve const *ResolveGeneralAnimCurve(ITSCENE::SceneDb const& scene, std::string const& nodeName, std::string const& attrName, float &outFactor)
{
	ITSCENE::GeneralCurveConnection const* generalConnect = GetGeneralCurveConnection(scene, nodeName, attrName);
	if (generalConnect) {
		outFactor = generalConnect->m_conversionFactor;
		return scene.GetGeneralAnimCurve(generalConnect->m_sourceNode);
	}
	return NULL;
}

// helper to set correct tangent/velocities values for HermiteSpline's from a ITSCENE type (which in turn matches Maya)
static inline HermiteSpline::Vector2 ConvertTangent(ITSCENE::TangentType const type, ITGEOM::Vec2 const& tangent)
{
	switch (type) {
	case ITSCENE::kTangentStep:
		return HermiteSpline::Vector2(0.0f);
	case ITSCENE::kTangentStepNext:
		return HermiteSpline::Vector2(FLT_MAX);
	default:
		return HermiteSpline::Vector2(tangent.x, tangent.y);
	}
}

static bool GeneralAnimCurveToHermiteSpline(HermiteSpline* dst, ITSCENE::GeneralAnimCurve const* src, float conversionFactor)
{
	static HermiteSpline::Infinity mapInfinityTypes[] = {
		HermiteSpline::kConstant,
		HermiteSpline::kLinear,
		HermiteSpline::kNone,
		HermiteSpline::kCycle,
		HermiteSpline::kOffset,
		HermiteSpline::kOscillate,
	};
	if (dst && src && !src->m_keyframes.empty()) {
		//dst->SetType( (ITSCENE::AnimCurve::CurveType)src->m_curveType );
		//dst->m_compression = ITSCENE::AnimCompression::kFnCurve;
		dst->setPreInfinity( mapInfinityTypes[src->m_preInfinityType] );
		dst->setPostInfinity( mapInfinityTypes[src->m_postInfinityType] );
		//dst->m_startValue = (float)src->m_keyframes[0].m_time;
		//dst->m_name = src->m_name;
		//dst->m_keys.clear();
		for (auto itKey = src->m_keyframes.begin(); itKey != src->m_keyframes.end(); itKey++) {
			HermiteSpline::Knot knot;
			knot.m_position.m_x = (float)itKey->m_time;
			knot.m_position.m_y = float(itKey->m_value * conversionFactor);
			knot.m_iVelocity = ConvertTangent((ITSCENE::TangentType)itKey->m_inTangentType, itKey->m_inTangent);
			knot.m_oVelocity = ConvertTangent((ITSCENE::TangentType)itKey->m_outTangentType, itKey->m_outTangent);
			dst->addKnot( knot );
		}
		return true;
	}
	return false;
}

static std::string GetNodeName(std::string& name)
{
	size_t i = name.find_last_of('.');
	if (i == name.npos) {
		return name;  // not found, just return name
	}
	return name.substr(0, i);
}

static std::string GetAttributeName(std::string& name)
{
	size_t i = name.find_last_of('.');
	if (i == name.npos) {
		return name;  // not found, just return name
	}
	return name.substr(i+1);
}

// gathers individual float track samples into vec3, quat or float channels
void CollectAnimationClipSourceData(AnimationClipSourceData& sourceData, AnimDataSource const& animDataSource, AnimationBinding const& binding)
{
	ITASSERT(animDataSource.m_pAnimScene);
	ITSCENE::SceneDb const& animScene = *animDataSource.m_pAnimScene;
	ITASSERT((animScene.m_flags & ITSCENE::BSF_EULER2QUAT) != 0);

	ITSCENE::JointAnimationList const& jointAnims = animScene.m_jointAnimations;
	ITSCENE::FloatAttributeAnimList const& floatAnims = animScene.m_floatAttributeAnims;
	
	ITASSERT(	binding.m_jointAnimIdToAnimatedJointIndex.size() == jointAnims.size() &&
				binding.m_jointAnimIdToJointId.size() == jointAnims.size() &&
				binding.m_floatAnimIdToFloatId.size() == floatAnims.size());

	unsigned const numJointAnims = (unsigned)jointAnims.size();
	unsigned const numFloatAnims = (unsigned)floatAnims.size();

	unsigned startFrame = (unsigned)animDataSource.m_frameSet.m_startFrame;
	unsigned endFrame = (unsigned)animDataSource.m_frameSet.m_endFrame;
	unsigned numSceneFrames = 0;

	sourceData.Init(numJointAnims, numFloatAnims);
	{
		std::string strValue;
		if (animDataSource.m_frameSet.m_eLoop == AnimFrameSet::kLoopDefault) {
			if (animScene.GetInfo( &strValue, "loopingAnim" ) && strValue != "" && strValue != "0") {
				sourceData.m_flags |= kClipLooping;
			}
		} else if (animDataSource.m_frameSet.m_eLoop == AnimFrameSet::kLoopLooping) {
			sourceData.m_flags |= kClipLooping;
		}
		if (animScene.m_flags & ITSCENE::BSF_ADDITIVE) {
			sourceData.m_flags |= kClipAdditive;
		}

		bool bMovementAnim = false;
		if (animDataSource.m_frameSet.m_eObjectRootAnim != AnimFrameSet::kObjectRootAnimDefault)
		{
			bMovementAnim = (animDataSource.m_frameSet.m_eObjectRootAnim != AnimFrameSet::kObjectRootAnimNone);
		}

		if (animDataSource.m_frameSet.m_endFrame != AnimFrameSet::kFrameEnd) {
			if (endFrame == startFrame) {
				sourceData.m_flags &= ~kClipLooping;		//single frame animations can not be looping
			}
			if (!(sourceData.m_flags & kClipLooping)) {
				++endFrame;		// if this is a non-looping animation, include the end frame
			} else if (bMovementAnim) {
				++endFrame;		// if this is a looping movement animation, include the end frame to be removed later
			}
		}
	}
	
	for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; ++iJointAnimIndex) {
		if (binding.m_jointAnimIdToJointId[iJointAnimIndex] < 0) {
			continue;
		}
		ITSCENE::NodeAnimation const* anim = jointAnims[iJointAnimIndex];
		{
			std::vector<float> samples_x, samples_y, samples_z;
			anim->GetSamples(ITSCENE::kAnimTrackScaleX, samples_x);
			anim->GetSamples(ITSCENE::kAnimTrackScaleY, samples_y);
			anim->GetSamples(ITSCENE::kAnimTrackScaleZ, samples_z);
			ITASSERT(samples_y.size() == samples_x.size() && samples_z.size() == samples_x.size());

			if (numSceneFrames == 0) {
				numSceneFrames = (unsigned)samples_x.size();
				ITASSERT(numSceneFrames > 0);
				if (endFrame >= numSceneFrames) endFrame = numSceneFrames;
				if (startFrame >= endFrame) startFrame = endFrame-1;
				sourceData.m_numFrames = endFrame - startFrame;
			} else {
				ITASSERT((unsigned)samples_x.size() == numSceneFrames);
			}

			ITGEOM::Vec3 sample_vec0( samples_x[startFrame], samples_y[startFrame], samples_z[startFrame] );
			ITGEOM::Vec3Array samples_vec3;
			samples_vec3.push_back(sample_vec0);
			for (unsigned i = startFrame + 1; i < endFrame; ++i) {
				ITGEOM::Vec3 sample_vec( samples_x[i], samples_y[i], samples_z[i] );
				samples_vec3.push_back(sample_vec);
			}

			float fScaleX = 0.0f, fScaleY = 0.0f, fScaleZ = 0.0f;
			ITSCENE::GeneralAnimCurve const* pScaleX = ResolveGeneralAnimCurve( animScene, anim->m_targetPathString, "scaleX", fScaleX );
			ITSCENE::GeneralAnimCurve const* pScaleY = ResolveGeneralAnimCurve( animScene, anim->m_targetPathString, "scaleY", fScaleY );
			ITSCENE::GeneralAnimCurve const* pScaleZ = ResolveGeneralAnimCurve( animScene, anim->m_targetPathString, "scaleZ", fScaleZ );

			if (pScaleX && pScaleY && pScaleZ) {
				std::vector<HermiteSpline> splines(3);
				GeneralAnimCurveToHermiteSpline( &(splines[0]), pScaleX, fScaleX );
				GeneralAnimCurveToHermiteSpline( &(splines[1]), pScaleY, fScaleY );
				GeneralAnimCurveToHermiteSpline( &(splines[2]), pScaleZ, fScaleZ );
				sourceData.AddSplineAnim( kChannelTypeScale, iJointAnimIndex, splines );
			} else {
				sourceData.AddSampledAnim(kChannelTypeScale, iJointAnimIndex, samples_vec3);
			}
		}
		{
			std::vector<float> samples_x, samples_y, samples_z, samples_w;
			anim->GetSamples(ITSCENE::kAnimTrackRotateX, samples_x);
			anim->GetSamples(ITSCENE::kAnimTrackRotateY, samples_y);
			anim->GetSamples(ITSCENE::kAnimTrackRotateZ, samples_z);
			anim->GetSamples(ITSCENE::kAnimTrackRotateW, samples_w);
			ITASSERT(samples_y.size() == samples_x.size() && samples_z.size() == samples_x.size() && samples_w.size() == samples_x.size());

			if (numSceneFrames == 0) {
				numSceneFrames = (unsigned)samples_x.size();
				ITASSERT(numSceneFrames > 0);
				if (endFrame >= numSceneFrames) endFrame = numSceneFrames;
				if (startFrame >= endFrame) startFrame = endFrame-1;
				sourceData.m_numFrames = endFrame - startFrame;
			} else {
				ITASSERT((unsigned)samples_x.size() == numSceneFrames);
			}

			ITGEOM::Quat sample_quat0( samples_x[startFrame], samples_y[startFrame], samples_z[startFrame], samples_w[startFrame] );
			ITGEOM::QuatArray samples_quat;
			samples_quat.push_back(sample_quat0);
			for (unsigned i = startFrame+1; i < endFrame; ++i) {
				ITGEOM::Quat sample_quat( samples_x[i], samples_y[i], samples_z[i], samples_w[i] );
				samples_quat.push_back(sample_quat);
			}

			float fRotateX = 0.0f, fRotateY = 0.0f, fRotateZ = 0.0f;
			ITSCENE::GeneralAnimCurve const* pRotateX = ResolveGeneralAnimCurve( animScene, anim->m_targetPathString, "rotateX", fRotateX );
			ITSCENE::GeneralAnimCurve const* pRotateY = ResolveGeneralAnimCurve( animScene, anim->m_targetPathString, "rotateY", fRotateY );
			ITSCENE::GeneralAnimCurve const* pRotateZ = ResolveGeneralAnimCurve( animScene, anim->m_targetPathString, "rotateZ", fRotateZ );

			if (pRotateX && pRotateY && pRotateZ) {
				std::vector<HermiteSpline> splines(3);
				GeneralAnimCurveToHermiteSpline( &(splines[0]), pRotateX, fRotateX );
				GeneralAnimCurveToHermiteSpline( &(splines[1]), pRotateY, fRotateY );
				GeneralAnimCurveToHermiteSpline( &(splines[2]), pRotateZ, fRotateZ );
				sourceData.AddSplineAnim( kChannelTypeRotation, iJointAnimIndex, splines );
			} else {
				sourceData.AddSampledAnim(kChannelTypeRotation, iJointAnimIndex, samples_quat);
			}
		}
		{
			std::vector<float> samples_x, samples_y, samples_z;
			anim->GetSamples(ITSCENE::kAnimTrackTranslateX, samples_x);
			anim->GetSamples(ITSCENE::kAnimTrackTranslateY, samples_y);
			anim->GetSamples(ITSCENE::kAnimTrackTranslateZ, samples_z);
			ITASSERT(samples_y.size() == samples_x.size() && samples_z.size() == samples_x.size());

			if (numSceneFrames == 0) {
				numSceneFrames = (unsigned)samples_x.size();
				ITASSERT(numSceneFrames > 0);
				if (endFrame >= numSceneFrames) endFrame = numSceneFrames;
				if (startFrame >= endFrame) startFrame = endFrame-1;
				sourceData.m_numFrames = endFrame - startFrame;
			} else {
				ITASSERT((unsigned)samples_x.size() == numSceneFrames);
			}

			ITGEOM::Vec3 sample_vec0( samples_x[startFrame], samples_y[startFrame], samples_z[startFrame] );
			ITGEOM::Vec3Array samples_vec3;
			samples_vec3.push_back(sample_vec0);
			for (unsigned i = startFrame+1; i < endFrame; ++i) {
				ITGEOM::Vec3 sample_vec( samples_x[i], samples_y[i], samples_z[i] );
				samples_vec3.push_back(sample_vec);
			}

			float fTranslateX = 0.0f, fTranslateY = 0.0f, fTranslateZ = 0.0f;
			ITSCENE::GeneralAnimCurve const* pTranslateX = ResolveGeneralAnimCurve( animScene, anim->m_targetPathString, "translateX", fTranslateX );
			ITSCENE::GeneralAnimCurve const* pTranslateY = ResolveGeneralAnimCurve( animScene, anim->m_targetPathString, "translateY", fTranslateY );
			ITSCENE::GeneralAnimCurve const* pTranslateZ = ResolveGeneralAnimCurve( animScene, anim->m_targetPathString, "translateZ", fTranslateZ );

			if (pTranslateX && pTranslateY && pTranslateZ) {
				std::vector<HermiteSpline> splines(3);
				GeneralAnimCurveToHermiteSpline( &(splines[0]), pTranslateX, fTranslateX );
				GeneralAnimCurveToHermiteSpline( &(splines[1]), pTranslateY, fTranslateY );
				GeneralAnimCurveToHermiteSpline( &(splines[2]), pTranslateZ, fTranslateZ );
				sourceData.AddSplineAnim( kChannelTypeTranslation, iJointAnimIndex, splines );
			} else {
				sourceData.AddSampledAnim(kChannelTypeTranslation, iJointAnimIndex, samples_vec3);
			}
		}
	}

	for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < numFloatAnims; ++iFloatAnimIndex) {
		if (binding.m_floatAnimIdToFloatId[iFloatAnimIndex] < 0) {
			continue;
		}
		ITSCENE::FloatAttributeAnim const* anim = floatAnims[iFloatAnimIndex];

		std::vector<float> samples;
		anim->GetSamples(samples);

		if (numSceneFrames == 0) {
			numSceneFrames = (unsigned)samples.size();
			ITASSERT(numSceneFrames > 0);
			if (endFrame >= numSceneFrames) endFrame = numSceneFrames;
			if (startFrame >= endFrame) startFrame = endFrame-1;
			sourceData.m_numFrames = endFrame - startFrame;
		} else {
			ITASSERT((unsigned)samples.size() == numSceneFrames);
		}

		std::vector<float> floatSamples(samples.begin()+startFrame, samples.begin()+endFrame);

		// search scene to see if the float attribute has an input "general anim curve" (a cubic Hermite spline)
		float conversionFactor = 0.0f;
		ITSCENE::GeneralAnimCurve const* scalarCurve = NULL;
		{
			std::string leafName = anim->m_targetPathString;
			//std::string leafName = ITSCENE::GetLeafName(anim->m_targetPathString);
			scalarCurve = ResolveGeneralAnimCurve(animScene, GetNodeName(leafName), GetAttributeName(leafName), conversionFactor);
		}
		
		// prefer to output Hermite splines if present in scene
		if (scalarCurve) {
			HermiteSpline spline;
			GeneralAnimCurveToHermiteSpline( &spline, scalarCurve, conversionFactor );
			sourceData.AddSplineAnim(kChannelTypeScalar, iFloatAnimIndex, spline);
		} else {
			sourceData.AddSampledAnim(kChannelTypeScalar, iFloatAnimIndex, floatSamples);
		}
	}

	if (sourceData.m_numFrames == 0) {
		sourceData.m_flags &= ~kClipLooping;		// single frame animations can not be looping
	}
}

void CollectAnimationClipSourceData(AnimationClipSourceData& sourceData, ITSCENE::SceneDb const& animScene, AnimationBinding const& binding)
{
	CollectAnimationClipSourceData(sourceData, AnimDataSource("LocalScene", &animScene), binding);
}

void CollectAnimationClipSourceDataFromBindPose(AnimationClipSourceData& sourceData, AnimationHierarchyDesc const& hierarchyDesc, AnimationBinding const& binding)
{
	ITASSERT(binding.m_hierarchyId == hierarchyDesc.m_hierarchyId);
	unsigned const numJoints = (unsigned)hierarchyDesc.m_joints.size();
	unsigned const numJointAnims = (unsigned)binding.m_jointAnimIdToJointId.size();
	unsigned const numFloatAnims = (unsigned)binding.m_floatAnimIdToFloatId.size();

	sourceData.Init(numJointAnims, numFloatAnims);
	sourceData.m_numFrames = 1;

	for (unsigned iJointAnimIndex = 0; iJointAnimIndex < numJointAnims; ++iJointAnimIndex) {
		int iJoint = binding.m_jointAnimIdToJointId[iJointAnimIndex];
		if (iJoint < 0) {
			continue;
		}
		ITASSERT((unsigned)iJoint < numJoints);
		Joint const& joint = hierarchyDesc.m_joints[iJoint];
		sourceData.AddSampledAnim(kChannelTypeScale, iJointAnimIndex, joint.m_jointScale);
		sourceData.AddSampledAnim(kChannelTypeRotation, iJointAnimIndex, joint.m_jointQuat);
		sourceData.AddSampledAnim(kChannelTypeTranslation, iJointAnimIndex, joint.m_jointTranslation);
	}

	for (unsigned iFloatAnimIndex = 0; iFloatAnimIndex < numFloatAnims; ++iFloatAnimIndex) {
		int iFloat = binding.m_floatAnimIdToFloatId[iFloatAnimIndex];
		if (iFloat < 0) {
			continue;
		}
		ITASSERT((unsigned)iFloat < hierarchyDesc.m_numFloatChannels);
		FloatChannel const& floatChannel = hierarchyDesc.m_floatChannels[iFloat];
		sourceData.AddSampledAnim(kChannelTypeScalar, iFloatAnimIndex, floatChannel.m_defaultValue);
	}
}

}	//namespace AnimProcessing
}	//namespace Tools
}	//namespace OrbisAnim
