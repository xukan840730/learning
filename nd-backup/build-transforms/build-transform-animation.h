/*
 * Copyright (c) 2015 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "tools/libs/exporters/bigexporter.h"
#include "tools/libs/toolsutil/farm.h"
#include "tools/pipeline3/build/build-transforms/build-transform.h"
#include "tools/pipeline3/build/build-transforms/build-transform-context.h"

/// --------------------------------------------------------------------------------------------------------------- ///
struct StreamAnim
{
	std::string m_name;
	std::string m_fullName;
	int m_animStreamChunkIndex;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct StreamBoEntry
{
	const libdb2::Anim* m_pAnim;
	std::string m_streamName;
	std::string m_boFileName;
	std::string m_animName;
	SkeletonId m_skelId;
	int m_index;
	int m_sampleRate;
	int m_numFrames;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct CommonAnimData
{
	CommonAnimData()
	{
		m_sampleRate = 0;

		m_isAdditive = false;
		m_isStreaming = false;
		m_isLooping = false;
		m_generateCenterOfMass = false;
		m_singleExport = false;						// Export all chunks with one ExportAnim for streaming animations
	}

	std::string m_animName;
	std::string m_animFullName;

	std::string m_exportNamespace;			// Namespace used to export the animation

	std::vector<const StreamAnim*> m_streamAnims;

	std::vector<std::string> m_partialSets;	// All partial sets used by this animation

	// Dependencies
	std::string m_skelSceneFile;			// Not a dependency but used to validate proper Builder setup
	SkeletonId m_skelId;
	std::string m_skelNdbFilename;
	std::string m_rigInfoFilename;

	U32 m_sampleRate;

	bool m_isAdditive;
	bool m_isStreaming;
	bool m_isLooping;
	bool m_generateCenterOfMass;
	bool m_singleExport;					// Used to export streaming animations in just one export, no chunking

	std::string m_jointCompression;
	std::string m_channelCompression;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimExportData
{
	AnimExportData()
	{
		m_startFrame = 0;
		m_endFrame = 0;

		m_realStartFrame = 0;
		m_restrictedStartFrame = 0;
		m_restrictedEndFrame = 0;
		m_ignorePartialSets = false;
	}

	std::string m_animFullPath;

	int32_t m_startFrame;						// Start frame of the animation
	int32_t m_endFrame;							// End frame of the animation

	// Used for non-complete animations (streaming)
	int32_t m_realStartFrame;					// Used when exporting chunks of streaming anims
	int32_t m_restrictedStartFrame;				// Start frame of what to actually export
	int32_t m_restrictedEndFrame;				// End frame of what to actually export

	std::string m_exportNamespace;			// Namespace used to export the animation
	std::string m_skelExportSet;			// Skeleton set used to filter inside of the selected export namespace
	bool m_ignorePartialSets;				// Ignore partial sets during export
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct AnimBuildData
{
	AnimBuildData(const libdb2::Anim* pDbAnim)
		: m_pDbAnim(pDbAnim)
		, m_actorRef(pDbAnim->m_actorRef)
		, m_startFrame()
		, m_endFrame()
		, m_exportCustomChannels(true)
		, m_headerOnly(false)
		, m_animBoFilename()
		, m_animBoJobId(0)
		, m_pStreamAnim(nullptr)
		, m_ignoreSkelDebugInfo(false)
	{}

	AnimBuildData(const std::string& actorRef)
		: m_pDbAnim(nullptr)
		, m_actorRef(actorRef)
		, m_startFrame()
		, m_endFrame()
		, m_exportCustomChannels(true)
		, m_headerOnly(false)
		, m_animBoFilename()
		, m_animBoJobId(0)
		, m_pStreamAnim(nullptr)
		, m_ignoreSkelDebugInfo(false)
	{}

	// Input
	const libdb2::Anim* m_pDbAnim;
	std::string m_actorRef;
	int32_t m_startFrame;
	int32_t m_endFrame;
	bool m_exportCustomChannels;
	bool m_headerOnly;

	// Output
	std::string m_animBoFilename;

	mutable FarmJobId m_animBoJobId;

	StreamAnim* m_pStreamAnim;
	bool m_ignoreSkelDebugInfo;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_ExportAnim : public BuildTransform
{
public:
	typedef bool (*InlineEvaluationFunc)(const std::string& options,
										 bool isStreaming,
										 const std::string animFullPath,
										 int sampleRate);

	BuildTransform_ExportAnim(const BuildTransformContext* const pContext,
							  const std::string& diskPath,
							  AnimExportData* const pExportData,
							  const CommonAnimData* const pCommonData,
							  InlineEvaluationFunc pInlineFunc);

	BuildTransformStatus Evaluate() override;
	BuildTransformStatus ResumeEvaluation(const SchedulerResumeItem& resumeItem) override;

	AnimExportData* m_pExportData;
	const CommonAnimData* m_pCommonData;

	enum InputIndex
	{
		SceneFilename = 0
	};

	enum OutputIndex
	{
		NdbFilename = 0
	};

private:
	std::string m_diskPath;

	FarmJobId m_maya3NdbJobId;
	InlineEvaluationFunc m_inlineEvaluationFunc;

	std::string CreateOptionsString(const AnimExportData* pExportData,
									const CommonAnimData* pCommonAnimData,
									const ToolParams& tool);
	bool KickAnimationExportJob(Farm& farm,
								const AnimExportData* pExportData,
								const CommonAnimData* pCommonAnimData,
								const ToolParams& tool);
	void ValidateSkeletonReference(const AnimExportData* pExportData,
								   const CommonAnimData* m_pCommonData,
								   const std::vector<std::pair<std::string, int>>& loadedReferences);
};

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_BuildAnim : public BuildTransform
{
public:
	enum InputIndex
	{
		SkelNdbFilename = 0,
		RigInfoFilename = 1,
		AnimNdbFilename = 2,
		RefAnimNdbFilename = 3
	};

	BuildTransform_BuildAnim(const BuildTransformContext* pContext,
							 AnimBuildData* pBuildData,
							 const CommonAnimData* pCommonData,
							 bool exportCameraCutAnim = false);

	BuildTransformStatus Evaluate() override;
	BuildTransformStatus ResumeEvaluation(const SchedulerResumeItem& resumeItem) override;

	void PopulatePreEvalDependencies();

	AnimBuildData* m_pBuildData;
	const CommonAnimData* m_pCommonData;

private:
	bool m_exportCameraCutAnim;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_AnimGroup : public BuildTransform
{
public:
	BuildTransform_AnimGroup(const BuildTransformContext* const pContext,
							 const std::string& actorName,
							 const SkeletonId skelId,
							 const std::vector<std::string>& animNames,
							 const std::vector<std::string>& animTagNames,
							 bool bLightAnims);

	BuildTransformStatus Evaluate() override;

private:

	std::string m_actorName;
	SkeletonId m_skelId;
	std::vector<std::string> m_animNames;
	std::vector<std::string> m_animTagNames;
	bool m_bLightAnims;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_BuildDebugSkelInfo : public BuildTransform
{
public:
	BuildTransform_BuildDebugSkelInfo(const BuildTransformContext* pContext,
									  const std::string& skelName,
									  bool isLightSkel = false)
		: BuildTransform("BuildSkelDebugInfo", pContext), m_skelName(skelName), m_isLightSkel(isLightSkel)
	{
		PopulatePreEvalDependencies();
	}

	virtual BuildTransformStatus Evaluate() override;

private:
	void PopulatePreEvalDependencies()
	{
		m_preEvaluateDependencies.SetString("skelName", m_skelName);
		m_preEvaluateDependencies.SetBool("isLightSkel", m_isLightSkel);
	}

	std::string m_skelName;
	bool m_isLightSkel;
};


/// --------------------------------------------------------------------------------------------------------------- ///
class BuildTransform_ValidateCinematicSequence : public BuildTransform
{
public:
	BuildTransform_ValidateCinematicSequence(const BuildTransformContext* pContext);

	virtual BuildTransformStatus Evaluate() override;

	struct Entry
	{
		std::string m_exportNamespace;
		unsigned m_sampleRate;
	};

	std::vector<Entry> m_entries;
};


/// --------------------------------------------------------------------------------------------------------------- ///
// Configure all the animations for each actor in the list, this is the path for build.exe
void BuildModuleAnimation_ConfigureFromActorList(const BuildTransformContext* const pContext,
												 const libdb2::Actor* const pDbActor,
												 const std::vector<const libdb2::Actor*>& actorList,
												 std::vector<std::string>& arrBoFiles,
												 std::string& actorTxtEntries);

/// --------------------------------------------------------------------------------------------------------------- ///
// Configure all the animations in a list, this is the path for live updating animations from Maya
void BuildModuleAnimation_ConfigureFromAnimationList(const BuildTransformContext* const pContext,
													 const std::vector<const libdb2::Anim*>& animList,
													 const libdb2::Actor* const pActor,
													 std::vector<std::string>& arrBoFiles,
													 std::vector<const BuildTransform*>& animXforms,
													 BuildTransform_ExportAnim::InlineEvaluationFunc pInlineFunc,
													 std::string& actorTxtEntries);

/// --------------------------------------------------------------------------------------------------------------- ///
inline unsigned int GetAnimSampleRate(const libdb2::Anim& anim, unsigned int sampleRate)
{
	if (anim.m_animationSampleRate.m_enabled)
	{
		unsigned int lsampleRate = (unsigned int)anim.m_animationSampleRate.m_value;
		if (lsampleRate)
			return lsampleRate;
	}
	return sampleRate;
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline SkeletonId GetSkelId(const libdb2::Actor& actor)
{
	const libdb2::Skeleton& skel = actor.m_skeleton;
	const std::string& setName = skel.m_set;
	std::string skelname = skel.m_sceneFile;
	if (setName.size())
		skelname += "." + setName;

	const SkeletonId skelID = ComputeSkelId(skelname);
	return skelID;
}

const int kStreamingChunkFinalIndex = 10000;

/// --------------------------------------------------------------------------------------------------------------- ///
inline std::string ConstructStreamName(const std::string& streamName, const std::string& fullName)
{
	// Is this a cinematic? If so, enforce a 'cinematic-name/cin-stream-bundle/cin-anim-name' naming convention
	size_t pos = fullName.find("/cin-stream");
	if (pos != std::string::npos)
	{
		// back up to the parent folder
		std::string fullParent = fullName.substr(0, pos);
		size_t posStart = fullParent.find_last_of("/");
		if (posStart != std::string::npos
			&&  fullParent.substr(posStart + 1, 4) == "cin-")
		{
			std::string cinematicName = fullParent.substr(posStart + 5);

			std::string bundleBaseName = fullName.substr(pos + 12); // skip past "/cin-stream-"
			size_t seqNoLen = bundleBaseName.find(".");
			ALWAYS_ASSERT(seqNoLen != std::string::npos);
			if (seqNoLen == 3)
			{
				std::string seqNoSuffix = bundleBaseName.substr(0, seqNoLen);
				const char* seqNoStr = seqNoSuffix.c_str();
				if (isdigit(seqNoStr[0]) && isdigit(seqNoStr[1]) && isdigit(seqNoStr[2]))
				{
					return std::string("cin-stream-") + cinematicName + "=" + seqNoSuffix;
				}
			}
		}
	}

	if (pos == std::string::npos)
		pos = fullName.find("/igc-stream");

	if (pos != std::string::npos)
	{
		size_t endPos = fullName.substr(pos + 1).find(".");
		ALWAYS_ASSERT(endPos != std::string::npos);

		return fullName.substr(pos + 1, endPos);
	}
	else
	{
		return streamName;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline bool StreamBoCompareFunc(StreamBoEntry& streamA, StreamBoEntry& streamB)
{
	// Sort by stream name, chunk index, animation name (slot). This will ensure proper interleaving.
	if (streamA.m_streamName == streamB.m_streamName)
	{
		if (streamA.m_index == streamB.m_index)
		{
			return streamA.m_pAnim->Filename() < streamB.m_pAnim->Filename();
		}
		else
		{
			return streamA.m_index < streamB.m_index;
		}
	}
	else
	{
		return streamA.m_streamName < streamB.m_streamName;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
inline void AddFloatAttribute(ITSCENE::SceneDb& scene, ITSCENE::FloatAttributeNode* fan, const std::string& attrName)
{
	size_t attrIndex = fan->AddAttrName(attrName, ITSCENE::kFaAnimOutput);
	ITSCENE::FloatAttribute* fa = fan->GetAttr(attrIndex);
	scene.m_floatAttributes.push_back(fa);
}