/*
* Copyright (c) 2005 Naughty Dog, Inc.
* A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
* Use and distribution without consent strictly prohibited
*/

#pragma once

#include <string>
#include "icelib/icesupport/streamwriter.h"
#include "animprocessingstructs.h"
#include "animerrortolerance.h"

namespace ITSCENE {
	class SceneDb;
	class SceneDb;
	class Joint;
	class FloatAttribute;
};

namespace OrbisAnim {
	namespace Tools {
		namespace ClipData {
			class ClipStats;
		}
		namespace AnimProcessing {
			struct AnimMetaData;
			struct ClipCompressionDesc;
			struct AnimationClipCompressionStats;
		}
	}
}

namespace OrbisAnim {
namespace Tools {
namespace AnimProcessing {

// Animation data processing involves a number of discrete steps:
// 1)	Collection and processing of data from the bind pose and animation scenes.
// 2)	Building of runtime data from the collected animation data.
// 3)	Writing the runtime data to an output stream.
//
// In the interest of providing a flexible library, we offer an interface in several layers:
// Scene level interface -
//		At the simplest level, the interface deals only with SceneDb and SceneDb
//		data, and provides a WriteClipData function that executes the standard iceanim.exe
//		style data collection and processing for steps 1 through 3.
//		Some functions are provided that pre-process SceneDb's.
// Intermediate data level interface -
//		At this level, the interface provides access to all animation source data in an
//		intermediate data format, and the scene level WriteClipData function can be
//		considered to be example code for the standard usage of the intermediate level
//		interface functions.  A second WriteClipData function is provided that executes
//		the standard iceanim.exe style data post-processing for steps 2 and 3.
// Runtime data level interface -
//		At this level, the interface additionally provides access to the animation data
//		in a format close to the runtime format, and to a set of functions that handle
//		the actual data compression process.  The scene and intermediate data level
//		WriteClipData functions can be considered to be example code for the usage of
//		the runtime data level interface functions.  The ClipDataWriter class is provided
//		to handle packing the compressed data together into a runtime ClipData structure
//		and writing it to a stream.
//
// The runtime data level functions are most likely to be updated by the ICE team with
// improvements and data format changes, and so most teams should consider the using the
// intermediate data level interface.  If using the intermediate data level functions and
// manipulations of the intermediate data structures is not sufficient for your needs,
// please consider contacting the ICE team, as we may consider adding features.
//
// NOTE: By nature, some manipulations of the data compression algorithms (for instance,
// animation time dependent manipulations of the error tolerances) are difficult to expose
// without exposing the potentially mutable details of the runtime format, so, for now,
// these remain part of the runtime level data interface.

//========================================================================================
// Scene level processing:				see animscenepreprocessing.cpp for all functions

void ExtractAdditiveAnimUsingBindPose(	ITSCENE::SceneDb& animScene,
										ITSCENE::SceneDb const& bindPoseScene);
void ExtractAdditiveAnimUsingReferenceAnimation(ITSCENE::SceneDb& animScene,
												ITSCENE::SceneDb const& refScene,
												int startFrame,
												bool useOnlyStartFrame);
//----------------------------------------------------------------------------------------
// Scene level write function:			see writeclipdata.cpp for function

// Reads meta data from outputAnimDesc.m_metaDataPath if it exists, and metadataFiles,
// all of which should be in animation meta data XML format, and fills out metaData.
// Returns true on success.
bool ReadMetaDataFromFiles(AnimMetaData& metaData,
						   OutputAnimDesc const& outputAnimDesc,
						   std::string const& bindPoseSceneFileName,
						   std::vector<std::string> const& metadataFiles);

// Extract the specified data from an animation scene relative to the given bind pose scene
// and write a ClipData with root anim as user data to streamWriter.
// Uses provided metaData instead of reading from outputAnimDesc.m_metaDataPath.
// Returns the location of the start of the ClipData written.
// 
// NOTE: Use ExtractClipDataIntermediateData and intermediate level WriteClipData
// below to write custom user data.
ICETOOLS::Location WriteClipData(ICETOOLS::StreamWriter& streamWriter,
								 ITSCENE::SceneDb const& bindPoseScene,
								 OutputAnimDesc const& outputAnimDesc,
								 AnimMetaData const& metaData,
								 ClipStats* clipStats = NULL);

// Extract the specified data from an animation scene relative to the given bind pose scene
// Write a bare 48-byte ClipData header and root anim as user data to streamWriter
// This is useful for writing ClipData headers to spoof a virtual ClipData which
// has alternate real ClipData substituted to the runtime internally, to spoof a single
// large ClipData for streaming which is internally replaced by a stream of real
// ClipData segments.
// Returns the location of the start of the ClipData written.
// 
// NOTE: Use ExtractClipDataIntermediateData and intermediate level WriteClipDataBareHeader
// below to write custom user data.
ICETOOLS::Location WriteClipDataBareHeader(ICETOOLS::StreamWriter& streamWriter,
										   ITSCENE::SceneDb const& bindPoseScene,
										   OutputAnimDesc const& outputAnimDesc);

//========================================================================================
// Intermediate data collection:		see animsceneprocessing.cpp for all functions

// Extract the specified data from an animation scene relative to the given bind pose scene
// Uses provided metaData instead of reading from outputAnimDesc.m_metaDataPath.
// Returns true on success.
bool ExtractClipDataIntermediateData(AnimationHierarchyDesc& hierarchyDesc,
									 AnimationBinding& binding,
									 AnimationClipProperties& clipProperties,
									 AnimationClipSourceData& sourceData,
									 ClipCompressionDesc& compression,
									 ITSCENE::SceneDb const& bindPoseScene,
									 OutputAnimDesc const& outputAnimDesc,
									 AnimMetaData const& metaData,
									 AnimationClipCompressionStats* stats = NULL);

// Extract the specified data from an animation scene relative to the given bind pose scene
// Returns true on success.
bool ExtractClipDataIntermediateData(AnimationHierarchyDesc& hierarchyDesc,
									 AnimationBinding& binding,
									 AnimationClipProperties& clipProperties,
									 AnimationClipSourceData& sourceData,
									 ITSCENE::SceneDb const& bindPoseScene,
									 OutputAnimDesc const& outputAnimDesc);

// Utility function to find a joint by name in a Joint list
int FindJointIndexByName(std::vector<Joint> const& joints, 
						 std::string const& name);

// Utility function to find a floatChannel by name in a FloatChannel list
int FindFloatChannelIndexByName(std::vector<FloatChannel> const& floatChannels,
								std::string const& name);

// Read AnimationHierarchyDesc hierarchy properties from a bind pose scene.
bool GetAnimationHierarchyDesc(AnimationHierarchyDesc& hierarchyDesc,
							   ITSCENE::SceneDb const& preppedBindPoseScene);

// Map an animated joint index to an index into hierarchyDesc.m_joints
int AnimatedJointIndexToJointId(AnimationHierarchyDesc const& hierarchyDesc,
								unsigned iAnimatedJointIndex );

// Map an index into hierarchyDesc.m_joints to an animated joint index
int JointIdToAnimatedJointIndex(AnimationHierarchyDesc const& hierarchyDesc,
								unsigned iJoint );

// Match the animations in animScene to the joints and float channels of hierarchyDesc by name,
// and return the resulting mapping.
// Note that binding.jointIdToJointAnimId may be undefined (-1) where no animation is attached to a joint,
// and that binding.jointAnimIdToJointId may be undefined (-1) if an animation is invalid or can't be
// matched to a joint.  The same is true of the float channel equivalents.
void BindAnimationScene(AnimationBinding& animBinding,
						AnimationHierarchyDesc const& hierarchyDesc,
						ITSCENE::SceneDb const& animScene);

// Build an identity binding for processing a bind pose
void BuildIdentityBinding(AnimationBinding& animBinding,
						  AnimationHierarchyDesc const& hierarchyDesc);

// Read AnimationClipProperties from animDataSource and perform some basic validation.
// Collects looping behavior, object root anim, additive flag, and frame rate from the spec
// or it's animation scene.
void CollectAnimationClipProperties(AnimationClipProperties& clipProperties,
									AnimDataSource const& animDataSource);

// Read AnimationClipProperties from animScene and perform some basic validation.
// Collects looping behavior, object root anim, additive flag, and frame rate from the animation scene.
void CollectAnimationClipProperties(AnimationClipProperties& clipProperties,
									ITSCENE::SceneDb const& animScene);

// Given an AnimMetaData structure containing data read from a metadata xml file with
// ProcessMetadataFile() or otherwise constructed, match it to the given binding and
// hierarchyDesc by channel names to construct a table of error tolerances per
// animation channel in constErrors.
void BindAnimMetaDataConstantErrorTolerancesToClip(ClipLocalSpaceErrors& tolerances,
												   AnimMetaData const& metaData,
												   AnimationHierarchyDesc const& hierarchyDesc,
												   AnimationBinding const& binding);

// Based on the given animDataSource, collect animation data from
// outputAnimDesc.m_pAnimScene into sourceData, possibly extracting a specified range
// of key frames.
// Uses binding to control which channels are extracted, and performs constant
// compression using constErrors to control which channels are compressed to constants.
void CollectAnimationClipSourceData(AnimationClipSourceData& sourceData,
									AnimDataSource const& animDataSource,
									AnimationBinding const& binding);

// Read all animation data from animScene into sourceData, using binding to control
// which channels are extracted, and constErrors to control which will be compressed
// as constants and which as animated channels.
void CollectAnimationClipSourceData(AnimationClipSourceData& sourceData,
									ITSCENE::SceneDb const& animScene,
									AnimationBinding const& binding);
		
// Read the bind pose from hierarchyDesc as a 1 frame animation into sourceData,
// using binding to control which channels are extracted.
void CollectAnimationClipSourceDataFromBindPose(AnimationClipSourceData& sourceData,
												AnimationHierarchyDesc const& hierarchyDesc,
												AnimationBinding const& binding);

//----------------------------------------------------------------------------------------
// Intermediate data processing:		see animprocessing.cpp for all functions

// Merge the animation iParentAnim, minus the parent bind pose, into iJointAnim by
// concatenating the transforms.
// The parent animation may not have non-uniform scale, as this will produce transforms not
// representable as S,Q,T triplets.
void MergeParentAnimation(AnimationClipSourceData& sourceData,
						  unsigned iParentAnim,
						  unsigned iJointAnim,
						  Joint const& parentJoint);

// Function prototype for a function to subtract part of the animation of
// sourceData joint animation iObjectRootAnim and store it in sourceData.m_pRootAnim.
// For the extracted root animation to work properly, the remaining animation
// left in sourceData must have the property that the last frame matches the
// first frame.
typedef bool (*ExtractRootAnimFunction)( AnimationClipSourceData& sourceData, unsigned iObjectRootAnim );

// Merges animations of all parents of joint hierarchyDesc.iObjectRootJoint into the animation for
// the object root joint, subtracts the clipRootAnim part from sourceData and returns it in clipRootAnim.
// If this is a looping movement animation, drops the final frame from sourceData to match the
// looping convention of having the final frame be an implicit copy of the first.
// If the resulting movement is not identity, sets clipProperties.objectRootAnim to clipRootAnim.GetType().
// Returns false and prints a message on error.
bool ExtractObjectRootAnim(ExtractRootAnimFunction pExtractRootAnimFunction,
						   AnimationClipSourceData& sourceData,
						   AnimationClipProperties& clipProperties,
						   AnimationHierarchyDesc const& hierarchyDesc,
						   AnimationBinding const& binding);

// Calculates sourceData = left - right, using leftBinding and rightBinding to match up the animation data.
// Returns the resulting binding in binding, after eliminating all identity and non-calculable channels.
// Steps right forward fRightFrameStep frames for each frame of left, starting from fRightFrameStart
// and clamping or looping as appropriate for right data.
// NOTE: sourceData and binding may output to the one of the input data sources.
void SubtractAnimation(AnimationClipSourceData& sourceData,
					   AnimationBinding& binding,
					   AnimationClipSourceData const& left,
					   AnimationBinding const& leftBinding,
					   AnimationClipSourceData const& right,
					   AnimationBinding const& rightBinding,
					   float fRightFrameStart,
					   float fRightFrameStep);

// Given an AnimMetaData structure containing data read from a metadata xml file with
// ProcessMetadataFile() or otherwise constructed, match it to the given sourceData by channel names
// and build a specific compression scheme to apply to this clip in compression.
bool BindAnimMetaDataToClip(ClipCompressionDesc& compression,
							AnimMetaData const& metaData,
							AnimationHierarchyDesc const& hierarchyDesc,
							AnimationBinding const& binding,
							AnimationClipSourceData& sourceData,
							AnimationClipCompressionStats *pStats = NULL);

//----------------------------------------------------------------------------------------
// Intermediate level write function:			see writeclipdata.cpp for function

// Compress the given sourceData using the compression scheme specified in compression
// and write a runtime ClipData to streamWriter, ready for custom user data to be written.
// Returns the stream Location of the start of the ClipData structure written.
// Fills out locTotalSize with the location of U32 m_totalSize in the ClipData header, which
// must be patched with the final size, and locUserDataOffset with the location of the
// U32 m_userDataOffset which may be patched with the location of a user data header.
ICETOOLS::Location WriteClipData(ICETOOLS::StreamWriter& streamWriter,
								 AnimationHierarchyDesc const& hierarchyDesc,
								 AnimationBinding const& binding,
								 AnimationClipProperties const& clipProperties,
								 AnimationClipSourceData const& sourceData,
								 ClipCompressionDesc const& compression,
								 ICETOOLS::Location& locTotalSize,
								 ICETOOLS::Location& locUserDataOffset,
								 ClipStats* clipStats = NULL);

// Write a bare 48-byte runtime ClipData header to streamWriter
// ready for custom user data to be written.
// This is useful for writing ClipData headers to spoof a virtual ClipData which
// has alternate real ClipData substituted to the runtime internally, to spoof a single
// large ClipData for streaming which is internally replaced by a stream of real
// ClipData segments.
// Returns the stream Location of the start of the ClipData structure written.
// Fills out locTotalSize with the location of U32 m_totalSize in the ClipData header, which
// must be patched with the final size, and locUserDataOffset with the location of the
// U32 m_userDataOffset which may be patched with the location of a user data header.
ICETOOLS::Location WriteClipDataBareHeader(ICETOOLS::StreamWriter& streamWriter,
										   AnimationHierarchyDesc const& hierarchyDesc,
										   AnimationClipProperties const& clipProperties,
										   AnimationClipSourceData const& sourceData,
										   ICETOOLS::Location& locTotalSize,
										   ICETOOLS::Location& locUserDataOffset);

//========================================================================================
// Runtime data functions:						see animprocessing.cpp for all functions

// Build shared key frame array for the sourceData based on the specified compression scheme.
// compression must specify shared keyframe compression.
void BuildAnimationClipSharedKeyFrameArray(FrameArray& keyFrames,
										   ClipCompressionDesc const& compression,
										   AnimationClipSourceData const& sourceData);

// Build data describing the layout of unshared key frame blocks for the sourceData based on
// the specified compression scheme.
// compression must specify unshared keyframe compression.
void BuildAnimationClipUnsharedKeyFrameBlocks(AnimationClipUnsharedKeyFrameBlockDesc& keyFrames,
											  ClipCompressionDesc const& compression,
											  AnimationBinding const& animBinding,
											  AnimationClipSourceData const& sourceData,
											  AnimationHierarchyDesc const& hierarchyDesc);

// Build compression version of sourceData based on compression scheme and keyframe compression
// encapsulated in keyExtractor.
void BuildAnimationClipCompressedData(AnimationClipCompressedData& compressedData,
									  AnimationBinding const& binding,
									  AnimationClipSourceData const& sourceData,
									  ClipCompressionDesc const& compression,
									  KeyExtractor const& keyExtractor);

// Build compression version of sourceData based on compression scheme and keyframe compression
// encapsulated in keyExtractor.
// Optimize world space errors by walking joint hierarchy, tweaking child joints to compensate for
// errors in their parent's compression.
void BuildAnimationClipCompressedDataOptimizedGlobally(AnimationClipCompressedData& compressedData,
													   AnimationBinding const& binding,
													   AnimationClipSourceData const& sourceData,
													   ClipCompressionDesc const& compression,
													   KeyExtractor const& keyExtractor,
													   std::vector<Joint> const& joints);

//----------------------------------------------------------------------------------------
// Runtime data level write function:			see class ClipDataWriter in clipdatawriter.h

//========================================================================================

}	//namespace AnimProcessing
}	//namespace Tools
}	//namespace OrbisAnim

