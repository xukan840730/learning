/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */


#include "tools/pipeline3/build/stdafx.h"

#include "tools/pipeline3/build/build-transforms/build-transform-animation.h"
#include "tools/pipeline3/build/build-transforms/build-transform-motion-matching.h"

#include <iostream>
#include <fstream>
#include <set>
#include <cctype>

#include "tools/libs/bigstreamwriter/ndi-bo-writer.h"
#include "tools/libs/toolsutil/command-job.h"
#include "tools/libs/toolsutil/farm.h"
#include "tools/libs/toolsutil/simpledb.h"
#include "tools/libs/toolsutil/simple-dependency.h"
#include "tools/libs/toolsutil/strfunc.h"
#include "tools/libs/writers/effect-table.h"
#include "tools/pipeline3/common/push-to-farm.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/blobs/blob-cache.h"
#include "tools/pipeline3/common/options.h"

#include "tools/pipeline3/toolversion.h"

#include "tools/pipeline3/build/tool-params.h"
#include "tools/pipeline3/build/actor/ba_havok.h"
#include "tools/pipeline3/build/build-transforms/build-transform-anim-stream.h"
#include "tools/pipeline3/build/build-transforms/build-transform-animation.h"
#include "tools/pipeline3/build/build-transforms/build-transform-skeleton.h"
#include "tools/pipeline3/build/build-transforms/build-transform-eff-generator.h"
#include "tools/pipeline3/build/util/dependency-database-manager.h"

#include "ice/src/tools/icelib/animprocessing_orbisanim/animprocessing.h"
#include "ice/src/tools/icelib/hierprocessing_orbisanim/hierprocessing.h"

#include "tools/pipeline3/common/path-prefix.h"
#include "common/util/timer.h"


using std::string;
using std::vector;

//#pragma optimize("", off) // uncomment when debugging in release mode

extern void BuildJointHierarchySegments(OrbisAnim::Tools::HierProcessing::JointHierarchySegments& segments,
										const BuildPipeline::RigInfo* pRigInfo);


extern std::string ExtraMaya3NDBOptions(const ToolParams &tool);

static const int kNumStreamingChunksPerExportBlock = 10;

/// --------------------------------------------------------------------------------------------------------------- ///
std::string BuildTransform_ExportAnim::CreateOptionsString(const AnimExportData* pExportData, const CommonAnimData* pCommonAnimData, const ToolParams& tool)
{
	const BuildFile& sceneInputFilename = GetInputs()[InputIndex::SceneFilename].m_file;
	string relativeFilename = sceneInputFilename.AsRelativePath();

	std::string options = relativeFilename + " ";

	char tmp[1024];

	options += " -anim " + pExportData->m_animFullPath;

	sprintf(tmp, " -startframe %i", pExportData->m_startFrame);
	options += std::string(tmp);
	sprintf(tmp, " -endframe %i", pExportData->m_endFrame);
	options += std::string(tmp);

	if (pCommonAnimData->m_isStreaming)
	{
		sprintf(tmp, " -restrictedstartframe %i", pExportData->m_restrictedStartFrame);
		options += std::string(tmp);
		sprintf(tmp, " -restrictedendframe %i", pExportData->m_restrictedEndFrame);
		options += std::string(tmp);

		sprintf(tmp, " -alignfirstframe %i", pExportData->m_realStartFrame);
		options += std::string(tmp);

		// Speed up multiple executions on the same host if all of them want to download the same file
 		options += " -cachesource ";
	}

	sprintf(tmp, " -samplerate %i", pCommonAnimData->m_sampleRate);
	options += std::string(tmp);

	if (!pCommonAnimData->m_partialSets.empty() && !pExportData->m_ignorePartialSets)
	{
		std::string partialSetsString;
		for (int i = 0; i < pCommonAnimData->m_partialSets.size(); ++i)
		{
			const std::string& partialSet = pCommonAnimData->m_partialSets[i];
			if (partialSetsString.size() != 0)
				partialSetsString += ",";
			partialSetsString += partialSet;
		}

		// Wrap the partial set list in single quotes so the ITCMDLine Parser detects it as a list of strings
		if (m_inlineEvaluationFunc)
		{
			options += " -partialsets '" + partialSetsString + "'";
		}
		else // Don't wrap when sending the list to maya3ndb because it breaks the sql message to the farm
		{
			options += " -partialsets " + partialSetsString;
		}
	}

	if (!pExportData->m_exportNamespace.empty())
	{
		// Pass the -namespace argument, which enables the "load only the necessary referenced .ma files" optimization.
		// NB: If we can't determine the namespace, we MUST load all referenced rigs or we'll get nada!
		options += " -animnamespace " + pExportData->m_exportNamespace;
	}

	if (!pExportData->m_skelExportSet.empty())
	{
		options += " -skelset " + pExportData->m_skelExportSet;
	}

	// To be removed! - We should not be able to read the XML file from Maya3Ndb
	std::string dbPath = toolsutils::GetGlobalDbPath(tool.m_host);
	options += " -dbpath " + dbPath;
	options += " -xmlfile " + pCommonAnimData->m_animFullName;

	options += " -user " + tool.m_userName + " ";
	options += " -host " + tool.m_host + " ";
	if (tool.m_local)
		options += " -local ";

	const TransformOutput& ndbOutput = GetFirstOutput();
	options += " -output " + ndbOutput.m_path.AsAbsolutePath();

	options += ExtraMaya3NDBOptions(tool);

	options += " -verbose ";

	return options;
}

bool BuildTransform_ExportAnim::KickAnimationExportJob(Farm& farm, const AnimExportData* pExportData, const CommonAnimData* pCommonAnimData, const ToolParams& tool)
{
	AutoTimer submitAnimJobsTimer("KickAnimationExportJobs");

	std::string options = CreateOptionsString(pExportData, pCommonAnimData, tool);

	IASSERT(!tool.m_executablesPath.empty());
	const std::string exePath = tool.m_executablesPath + "/maya3ndb.app/" + "maya3ndb.exe";

	JobDescription job(exePath, options, tool.m_localtools, tool.m_local,  tool.m_x64maya);
	job.m_useSetCmdLine = true;

	const int numFramesToExport = pExportData->m_endFrame - pExportData->m_startFrame;
	const int numRestrictedFramesToExport = pExportData->m_restrictedEndFrame - pExportData->m_restrictedStartFrame;
	const int numActualFramesToExport = numFramesToExport < numRestrictedFramesToExport ? numFramesToExport : numRestrictedFramesToExport;

	// yes 5Gb it's been clocked on the farm unfortunately and it dies when building too many of those simultaneously
	// Let's attribute the memory requirement to the number of frames exported.
	const unsigned long long kOneMiB = 1024ULL * 1024ULL;
	unsigned long long requiredMem = numActualFramesToExport > 500 ? 5000ULL * kOneMiB : 2500ULL * kOneMiB;
	if (options.find("/cin-stream-") != std::string::npos)		// some cinematics take up 12Gb of memory, so let's inflate the requirement
		requiredMem = 9000ULL * kOneMiB;

	const unsigned int requiredThreads = 1;
	m_maya3NdbJobId = farm.submitJob(job.GetCommand(), requiredMem, requiredThreads);

	return false;
}


static int CopyFileToNetDB(const std::string& dbRootPath, const std::string& netRootPath, const std::string &filename)
{
	std::string netFilename = filename;
	size_t pos = netFilename.find(dbRootPath);
	if (pos == std::string::npos)
	{
		netFilename = netRootPath + "/" + netFilename;
	}
	else
	{
		netFilename.replace(pos, dbRootPath.length(), netRootPath);
	}

//	printf("############# %s -> %s ################\n", filename.c_str(), netFilename.c_str());

	if(!toolsutils::CreatePath(netFilename.c_str()))
	{
		return -1;
	}

	Err err = FileIO::copyFile(netFilename.c_str(), filename.c_str());
	if (err.Failed()) return -1;
	if (filename.rfind("link") == filename.length()-strlen("link"))  // in case of a link copy, recurse to copy the target
	{
		std::string targetFile;
		std::ifstream file(filename.c_str(), std::ios_base::in + std::ios_base::binary);
		if (file.is_open())
		{
			file >> targetFile;
			file.close();
		}
		if (targetFile != "")
			return CopyFileToNetDB(dbRootPath,  netRootPath, dbRootPath + "/data/db/builderdb/"+ targetFile);  // links so far only exist in the builder db...
	}
	return 0;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_ExportAnim::BuildTransform_ExportAnim(const BuildTransformContext* const pContext,
													 const std::string& diskPath,
													 AnimExportData* const pExportData,
													 const CommonAnimData* const pCommonData,
													 InlineEvaluationFunc pInlineFunc)
	: BuildTransform("ExportAnim", pContext)
	, m_diskPath(diskPath)
	, m_pExportData(pExportData)
	, m_pCommonData(pCommonData)
	, m_maya3NdbJobId(0)
	, m_inlineEvaluationFunc(pInlineFunc)
{
	// ensure this gets into the db
	if (m_pCommonData)
	{
		StringToStringId64(m_pCommonData->m_animName.c_str(), true);
	}

	m_preEvaluateDependencies.SetConfigString("transformVersion", BUILD_TRANSFORM_ANIM_EXPORT_VERSION);

	if (m_pContext->m_toolParams.m_baseMayaVer.size())
	{
		m_preEvaluateDependencies.SetString("mayaVersion", m_pContext->m_toolParams.m_baseMayaVer);
	}

	m_preEvaluateDependencies.SetInt("startFrame", m_pExportData->m_startFrame);
	m_preEvaluateDependencies.SetInt("endFrame", m_pExportData->m_endFrame);
	if (m_pCommonData->m_isStreaming)
	{
		// We can restrict which frames we actually sample the animations vs write out identity information for.
		// This is to speed up extraction of really long animations
		m_preEvaluateDependencies.SetInt("restrictedStartFrame", m_pExportData->m_restrictedStartFrame);
		m_preEvaluateDependencies.SetInt("restrictedEndFrame", m_pExportData->m_restrictedEndFrame);
		m_preEvaluateDependencies.SetInt("alignFirstFrame", m_pExportData->m_realStartFrame);
	}

	m_preEvaluateDependencies.SetInt("sampleRate", m_pCommonData->m_sampleRate);

	m_preEvaluateDependencies.SetString("animExportNamespace", m_pExportData->m_exportNamespace);
	m_preEvaluateDependencies.SetString("skelExportSet", m_pExportData->m_skelExportSet);

	m_preEvaluateDependencies.SetBool("fullRigExport", m_pContext->m_toolParams.m_fullRig);			// We need to differentiate between using the reference skeleton and using the actual referenced scene file

																									// Add the partial sets
	char keyNameBuffer[1024];
	for (size_t partialSetIndex = 0; partialSetIndex < m_pCommonData->m_partialSets.size(); ++partialSetIndex)
	{
		sprintf(keyNameBuffer, "partialSet%d", partialSetIndex);
		m_preEvaluateDependencies.SetString(keyNameBuffer, m_pCommonData->m_partialSets[partialSetIndex]);
	}

	if (m_inlineEvaluationFunc)
	{
		// Currently only the animation reload command in Maya is using a custom inline evaluation function, 
		// so in this case, we want to always evaluate the ExportAnim transform so animators don't have to always save their Maya scenes
		// in order to reload/re-export animations.
		SetDependencyMode(DependencyMode::kIgnoreDependency);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_ExportAnim::Evaluate()
{
	if (m_inlineEvaluationFunc)
	{
		std::string options = CreateOptionsString(m_pExportData, m_pCommonData, m_pContext->m_toolParams);
		bool success = m_inlineEvaluationFunc(options, m_pCommonData->m_isStreaming, m_pExportData->m_animFullPath, m_pCommonData->m_sampleRate);
		return success ? BuildTransformStatus::kOutputsUpdated : BuildTransformStatus::kFailed;
	}

	const std::string& dbRootPath = m_pContext->m_toolParams.m_dbPath;
	const std::string netRootPath = std::string("x:/build/") + NdGetEnv("USERNAME") + "/" + NdGetEnv("GAMENAME");
	CopyFileToNetDB(dbRootPath, netRootPath, m_diskPath);

	EffectMasterTable::SetDBPath(m_pContext->m_toolParams.m_dbPath);

	KickAnimationExportJob(m_pContext->m_buildScheduler.GetFarmSession(), m_pExportData, m_pCommonData, m_pContext->m_toolParams);
	m_pContext->m_buildScheduler.RegisterFarmWaitItem(this, m_maya3NdbJobId);
	return BuildTransformStatus::kResumeNeeded;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static char BackSlashToForwardSlash(char value)
{
	if (value == '\\')
		return '/';

	return value;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform_ExportAnim::ValidateSkeletonReference(const AnimExportData* pExportData,
														  const CommonAnimData* m_pCommonData,
														  const std::vector<std::pair<std::string, int>>& loadedReferences)
{
	const std::string& skelSceneFile = m_pCommonData->m_skelSceneFile;

	const BuildFile& sceneFilePath = GetInputs()[InputIndex::SceneFilename].m_file;
	string animationSceneFile = sceneFilePath.AsRelativePath();

	std::string skelSceneFileLowerCase = skelSceneFile;
	std::string animationSceneFileLowerCase = animationSceneFile;
	std::transform(skelSceneFileLowerCase.begin(), skelSceneFileLowerCase.end(), skelSceneFileLowerCase.begin(), std::tolower);
	std::transform(animationSceneFileLowerCase.begin(), animationSceneFileLowerCase.end(), animationSceneFileLowerCase.begin(), std::tolower);
	std::transform(skelSceneFileLowerCase.begin(), skelSceneFileLowerCase.end(), skelSceneFileLowerCase.begin(), BackSlashToForwardSlash);
	std::transform(animationSceneFileLowerCase.begin(), animationSceneFileLowerCase.end(), animationSceneFileLowerCase.begin(), BackSlashToForwardSlash);

	// Create the list of all processed Maya files when exporting this animation
	std::vector<std::string> processedMayaFiles;

	processedMayaFiles.push_back(animationSceneFileLowerCase);
	for (auto path : loadedReferences)
	{
		std::string referenceFile = path.first;
		std::transform(referenceFile.begin(), referenceFile.end(), referenceFile.begin(), std::tolower);
		std::transform(referenceFile.begin(), referenceFile.end(), referenceFile.begin(), BackSlashToForwardSlash);
		processedMayaFiles.push_back(referenceFile);
	}

	// Look for the skeleton scene file in the list of processed Maya files for the animation
	const auto& findIter = std::find(processedMayaFiles.begin(), processedMayaFiles.end(), skelSceneFileLowerCase);
	if (findIter == processedMayaFiles.end())
	{
		bool foundSkel = false;

		// It is possible that we replaced the 'deformation_rig' with 'reference_skeleton'. Try that.
		size_t pos = skelSceneFileLowerCase.find("deformation_rig.ma");
		if (pos != std::string::npos && skelSceneFileLowerCase.find("master_deformation_rig.ma") == std::string::npos)
		{
			std::string replacementFilename = skelSceneFileLowerCase.substr(0, pos) + "reference_skeleton.ma";
			const auto& findIter2 = std::find(processedMayaFiles.begin(), processedMayaFiles.end(), replacementFilename);
			if (findIter2 != processedMayaFiles.end())
			{
				// Yay, we found the alternate filename!
				foundSkel = true;
			}
		}

		if (!foundSkel)
		{
			IERR("The Maya file used for the reference skeleton [%s] was not referenced by the animation scene.", skelSceneFile.c_str());
			IERR("This is a setup issue for animation '%s'.", m_pCommonData->m_animFullName.c_str());
			for (auto& refFile : loadedReferences)
			{
				IERR("Loaded reference file: %s", refFile.first.c_str());
			}
			IABORT("Is the animation using the correct refSkel actor?\n"
				"Are you referencing the correct skeleton scene in Maya?");
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_ExportAnim::ResumeEvaluation(const SchedulerResumeItem& resumeItem)
{
	const std::vector<std::pair<std::string, int>> loadedReferences = ExtractLoadedReferencesFromMaya3ndbOutput(m_pContext->m_buildScheduler, m_maya3NdbJobId);

	// Validate that the skeleton scene was actually referenced by the animation file
	ValidateSkeletonReference(m_pExportData, m_pCommonData, loadedReferences);

	// Add the loaded references
	const std::string& fsRoot = m_pContext->m_toolParams.m_fsRoot;
	for (const auto& ref : loadedReferences)
	{
		const BuildPath refPath(fsRoot + FileIO::separator + ref.first);
		RegisterDiscoveredDependency(refPath, ref.second);
	}

	return BuildTransformStatus::kOutputsUpdated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_BuildDebugSkelInfo::Evaluate()
{
	BigStreamWriterConfig streamCfg(m_pContext->m_toolParams.m_streamConfig);
	streamCfg.m_useIncrementalTags = true;

	const BuildFile& skelNdbFile = GetInputFile("SkelNdb");
	const BuildFile& rigInfoFile = GetInputFile("RigInfo");
	const BuildPath& skelBoFilename = GetOutputPath("SkelInfoBo");			// .bo file for the	skeleton generated in the Build step

	NdbStream skelNdbStream;
	DataStore::ReadData(skelNdbFile, skelNdbStream);

	NdbStream rigInfoStream;
	DataStore::ReadData(rigInfoFile, rigInfoStream);

	BuildPipeline::RigInfo rigInfo;
	rigInfo.NdbSerialize(rigInfoStream);

	BigStreamWriter streamWriter(streamCfg);

	{
		// Debug skel info
		streamWriter.StartItem(BigWriter::RAW_DATA, "debugSkelInfoLoc", "", SEG_DEBUG);

		OrbisAnim::Tools::HierProcessing::JointHierarchySegments segments;
		BuildJointHierarchySegments(segments, &rigInfo);

		streamWriter.Align(16);
		Location startOfData = streamWriter.CreatePosition();
		streamWriter.Write2(rigInfo.NumJoints());
		streamWriter.Write2(rigInfo.GetNumFloatChannels());
		streamWriter.Write2(segments.size());						// Num Segments
		streamWriter.Write2(0);										// Padding
		Location segDetailsLoc = streamWriter.WriteNullPointer();	// Segment details
		Location procGroupsLoc = streamWriter.WriteNullPointer();	// Processing group details 
		Location jointNamesLoc = streamWriter.WriteNullPointer();	// Joint names
		Location defaultPosesLoc = streamWriter.WriteNullPointer();	// Default SQTs

		streamWriter.Align(16);
		streamWriter.SetPointer(segDetailsLoc);
		int numAnimatedJoints = 0;
		for (U32 iSeg = 0; iSeg < rigInfo.NumSegments(); ++iSeg)
		{
			numAnimatedJoints += segments[iSeg].m_numAnimatedJoints;

			streamWriter.Write4(segments[iSeg].m_numAnimatedJoints);
			streamWriter.Write4(segments[iSeg].m_firstJoint);
			streamWriter.Write4(segments[iSeg].m_numJoints);
			streamWriter.Write2(segments[iSeg].m_firstProcessingGroup);
			streamWriter.Write2(segments[iSeg].m_processingGroups.size());
		}

		std::vector<unsigned> aiAnimatedJoint2Joint(numAnimatedJoints);
		int iAnimatedJoint = 0;
		for (U32 iSeg = 0; iSeg < rigInfo.NumSegments(); ++iSeg)
		{
			const size_t iFirstJoint = segments[iSeg].m_firstJoint;
			const size_t iJointEnd = segments[iSeg].m_firstJoint + segments[iSeg].m_numAnimatedJoints;
			for (size_t iJoint = iFirstJoint; iJoint < iJointEnd; ++iJoint)
			{
				aiAnimatedJoint2Joint[iAnimatedJoint++] = (unsigned)iJoint;
			}

			streamWriter.Write4(segments[iSeg].m_numAnimatedJoints);
			streamWriter.Write4(segments[iSeg].m_firstJoint);
			streamWriter.Write4(segments[iSeg].m_numJoints);
			streamWriter.Write2(segments[iSeg].m_firstProcessingGroup);
			streamWriter.Write2(segments[iSeg].m_processingGroups.size());
		}


		streamWriter.Align(16);
		streamWriter.SetPointer(procGroupsLoc);
		for (U32 iSeg = 0; iSeg < rigInfo.NumSegments(); ++iSeg)
		{
			for (U32 iProcGroup = 0; iProcGroup < segments[iSeg].m_processingGroups.size(); ++iProcGroup)
			{
				streamWriter.Write2(segments[iSeg].m_processingGroups[iProcGroup].m_numAnimatedJoints);
				streamWriter.Write2(segments[iSeg].m_processingGroups[iProcGroup].m_numChannelGroups);
				streamWriter.Write2(segments[iSeg].m_processingGroups[iProcGroup].m_numFloatChannels);
			}
		}

		streamWriter.Align(16);
		streamWriter.SetPointer(jointNamesLoc);

		// Write the joint IDs
		for (U32F iJoint = 0; iJoint < rigInfo.NumJoints(); ++iJoint)
		{
			streamWriter.WriteStringId64(StringToStringId64(rigInfo.GetJoint(iJoint).m_strIngameName.c_str(), true));
		}

		// Write the float channel IDs
		ITSCENE::SceneDb sceneDb;
		bool success = LoadSceneNdb(skelNdbStream, &sceneDb);
		for (U32F iFloat = 0; iFloat < sceneDb.m_floatAttributes.size(); ++iFloat)
		{
			std::string floatName = sceneDb.m_floatAttributes[iFloat]->GetFullName();
			size_t offset = floatName.find_last_of(":|");
			if (offset != std::string::npos)
				floatName = floatName.substr(offset + 1);

			streamWriter.WriteStringId64(StringToStringId64(floatName.c_str(), true));
		}

		// Write the default pose SQTs
		streamWriter.Align(16);
		streamWriter.SetPointer(defaultPosesLoc);

		for (size_t iAnimatedJoint = 0; iAnimatedJoint < numAnimatedJoints; iAnimatedJoint++)
		{
			size_t iJoint = aiAnimatedJoint2Joint[iAnimatedJoint];
			const ITSCENE::Joint* pJoint = sceneDb.m_joints[iJoint];

			// Default scale
			streamWriter.WriteF(pJoint->m_jointScale[0]);
			streamWriter.WriteF(pJoint->m_jointScale[1]);
			streamWriter.WriteF(pJoint->m_jointScale[2]);
			streamWriter.Write4(0);

			// Default rotation
			ITGEOM::Quat jointQuat;
			ITGEOM::QuatFromMayaEulerAngles(&jointQuat, pJoint->m_jointRotate, pJoint->m_rotateAxis, pJoint->m_jointOrient, pJoint->m_rotateOrder);
			streamWriter.WriteF(jointQuat.x);
			streamWriter.WriteF(jointQuat.y);
			streamWriter.WriteF(jointQuat.z);
			streamWriter.WriteF(jointQuat.w);

			// Default translation
			streamWriter.WriteF(pJoint->m_transformMatrix[3][0]);
			streamWriter.WriteF(pJoint->m_transformMatrix[3][1]);
			streamWriter.WriteF(pJoint->m_transformMatrix[3][2]);
			streamWriter.Write4(0);
		}


		streamWriter.EndItem();

		// Create an external location
		const std::string tagName = m_skelName + (m_isLightSkel ? ".light-debugskelinfo" : ".debugskelinfo");
		INOTE_VERBOSE("Tagging debug skel data with the tag '%s'", tagName.c_str());
		streamWriter.TagLocation(startOfData, tagName.c_str());		// tag example: 'base-male-skel.debugskelinfo'
	}

	NdiBoWriter boWriter(streamWriter);
	boWriter.Write();

	DataStore::WriteData(skelBoFilename, boWriter.GetMemoryStream());

	return BuildTransformStatus::kOutputsUpdated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool BuildAnim(Farm& farm,
					  const AnimBuildData* pBuildData,
					  const CommonAnimData* pCommonData,
					  const BuildFile& skelNdbFile,
					  const BuildFile& rigInfoFile,
					  const BuildFile& animNdbFile,
					  const BuildFile& effNdbFile,
					  const BuildFile& refAnimNdbFile,
					  const BuildPath& boFilename,
					  const ToolParams& tool,
					  bool exportCameraCutAnim)
{
	AutoTimer submitAnimJobsTimer("BuildAnims");

	std::stringstream options;
	options << pipeline3::Options::Current().GetTargetPlatformOption(pipeline3::Options::kAnim3bo);

 	options << " -user " << tool.m_userName;

	if (pCommonData->m_isStreaming && pBuildData->m_pStreamAnim)
		options << " -animName " << pBuildData->m_pStreamAnim->m_name;
	else
		options << " -animName " << pCommonData->m_animName;

	if (pCommonData->m_exportNamespace.size())
		options << " -animExportNamespace " << pCommonData->m_exportNamespace;

	if (pBuildData->m_ignoreSkelDebugInfo)
	{
		options << " -ignoreSkelDebugInfo";
	}

	std::string partialSetString;
	if (!pCommonData->m_partialSets.empty())
	{
		for (int i = 0; i < pCommonData->m_partialSets.size(); ++i)
		{
			const std::string& partialSet = pCommonData->m_partialSets[i];
			if (partialSetString.size() != 0)
				partialSetString += "#";
			partialSetString += partialSet + "," + partialSet;
		}
		options << " -partialSets " << partialSetString;
	}

	options << " -animNdbPath " << animNdbFile.AsAbsolutePath();
	options << " -animNdbHash " << animNdbFile.GetContentHash().AsText();
	if (refAnimNdbFile.IsValid())
	{
		options << " -refAnimNdbPath " << refAnimNdbFile.AsAbsolutePath();
		options << " -refAnimNdbHash " << refAnimNdbFile.GetContentHash().AsText();
	}
	options << " -skelNdbPath " << skelNdbFile.AsAbsolutePath();
	options << " -skelNdbHash " << skelNdbFile.GetContentHash().AsText();
	options << " -rigInfoNdbPath " << rigInfoFile.AsAbsolutePath();
	options << " -rigInfoNdbHash " << rigInfoFile.GetContentHash().AsText();

	if (effNdbFile.IsValid())
	{
		options << " -effNdbPath " << effNdbFile.AsAbsolutePath();
		options << " -effNdbHash " << effNdbFile.GetContentHash().AsText();
	}

	options << " -startFrame " << ToString(pBuildData->m_startFrame);
	options << " -endFrame " << ToString(pBuildData->m_endFrame);
 
 	options << " -sampleRate " << ToString(pCommonData->m_sampleRate);

	// Camera cut animations use normal compression
	if (exportCameraCutAnim)
	{
		options << " -jointCompression " << "normal";
		options << " -channelCompression " << "default";
	}
	else
	{
		options << " -jointCompression " << pCommonData->m_jointCompression;
		options << " -channelCompression " << pCommonData->m_channelCompression;
	}
 
 	if (pCommonData->m_isLooping)
 		options << " -looping ";
 	if (pCommonData->m_generateCenterOfMass)
 		options << " -exportCenterOfMass ";
	if (exportCameraCutAnim)
		options << " -exportCameraCutAnim ";

 	if (pBuildData->m_exportCustomChannels)
 		options << " -exportCustomChannels ";
 	if (pBuildData->m_headerOnly)
 		options << " -headerOnly ";

  	if (pCommonData->m_isStreaming && pBuildData->m_pStreamAnim)
	{
		if (exportCameraCutAnim)
		{
			options << " -addLoginItem ";
		}
		else
		{
			options << " -streamingChunk ";

			// Don't add login items for -chunk-0 and -chunk-last
			const bool lastChunk = pBuildData->m_pStreamAnim->m_name.find("-chunk-last") != std::string::npos;
			if (pBuildData->m_pStreamAnim->m_animStreamChunkIndex != 0 && !lastChunk)
				options << " -addLoginItem ";
		}
	}

	size_t pos1 = pBuildData->m_actorRef.rfind("/") + 1;
	size_t pos2 = pBuildData->m_actorRef.find(".actor.xml");
	options << " -skelName " << pBuildData->m_actorRef.substr(pos1, pos2 - pos1);

	options << " -output " << boFilename.AsAbsolutePath();

 	options << " -verbose ";

	IASSERT(!tool.m_executablesPath.empty());
	const std::string exePath = tool.m_executablesPath + "/anim3bo.app/anim3bo.exe";

	JobDescription job(exePath, options.str(), tool.m_localtools, tool.m_local, true /*x64exe*/);
	job.m_useSetCmdLine = true;

	uint64_t memory = 100 * 1024 * 1024;
	pBuildData->m_animBoJobId = farm.submitJob(job.GetCommand(), memory, 1, false, false, true);

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_BuildAnim::BuildTransform_BuildAnim(const BuildTransformContext* pContext,
												   AnimBuildData* pBuildData,
												   const CommonAnimData* pCommonData,
												   bool exportCameraCutAnim)
	: BuildTransform("BuildAnim", pContext)
	, m_pBuildData(pBuildData)
	, m_pCommonData(pCommonData)
	, m_exportCameraCutAnim(exportCameraCutAnim)
{
	PopulatePreEvalDependencies();
}

/// --------------------------------------------------------------------------------------------------------------- ///
static std::string GetAnimSkelBoPath(const libdb2::Anim& anim,
									 const std::string buildPath,
									 std::string& skelNameOut,
									 bool& errorsOccurred)
{
	static std::map<std::string, std::string> actorRefSkel;
	static std::map<std::string, std::string> actorRefSkelName;
	static thread::Mutex s_mutex;

	std::string skelBoFilename;

	s_mutex.lock();
	
	std::map<std::string, std::string>::iterator p = actorRefSkel.find(anim.m_actorRef);
	std::map<std::string, std::string>::iterator p2 = actorRefSkelName.find(anim.m_actorRef);

	const bool skelFound = p != actorRefSkel.end();
	const bool skelNameFound = p2 != actorRefSkelName.end();

	if (!skelFound || !skelNameFound)
	{
		const libdb2::Actor* pDbRefActor = libdb2::GetActor(anim.m_actorRef, false);
		if (!pDbRefActor->Loaded())
		{
			IERR("Unknown Actor '%s'\n", anim.m_actorRef.c_str()); 
			errorsOccurred = true;
		}

		if (!skelFound)
		{
			std::string actorRefBuildFolder = buildPath + pDbRefActor->FullName() + FileIO::separator;
			skelBoFilename = actorRefBuildFolder + "skel.bo";
			actorRefSkel[anim.m_actorRef] = skelBoFilename;
		}

		if (!skelNameFound)
		{
			if (pDbRefActor->m_skeleton.Loaded())
			{
				std::string skelName = pDbRefActor->m_skeleton.Name();
				skelNameOut = skelName;
				actorRefSkelName[anim.m_actorRef] = skelName;
			}
			else
			{
				skelNameOut = "SkelNameUndefined";
				IERR("skeleton not defined in %s. Please edit this actor in Builder and define the skeleton.", anim.m_actorRef.c_str());
				errorsOccurred = true;
			}
		}
	}
	else
	{
		skelBoFilename = p->second;
		skelNameOut = p2->second;
	}
	s_mutex.unlock();

	return skelBoFilename;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct NeedAnimBuildThreadData
{
	const AnimBuildData* m_pBuildData;
	const ToolParams* m_pTool;
	const EffectAnimTable* m_pEffectTable;

	// Status variables
	bool m_build;
	bool m_error;
	FileDateCache* m_pFileDateCache;
	const BuildTransformContext* m_pContext;
	BuildTransform_BuildAnim* m_pTransform;

	NeedAnimBuildThreadData() 
	{ 
		m_pBuildData = NULL;
		m_build = false;
		m_error = false;
		m_pFileDateCache = NULL;
		m_pContext = NULL;
		m_pTransform = NULL;
	}
};

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform_BuildAnim::PopulatePreEvalDependencies()
{
	m_preEvaluateDependencies.SetConfigString("transformVersion", BUILD_TRANSFORM_ANIM_BUILD_VERSION);

	// Input settings
	m_preEvaluateDependencies.SetInt("startFrame", m_pBuildData->m_startFrame);
	m_preEvaluateDependencies.SetInt("endFrame", m_pBuildData->m_endFrame);
	m_preEvaluateDependencies.SetBool("exportCustomChannels", m_pBuildData->m_exportCustomChannels);
	m_preEvaluateDependencies.SetBool("exportCameraCutAnim", m_exportCameraCutAnim);
	m_preEvaluateDependencies.SetBool("headerOnly", m_pBuildData->m_headerOnly);
	m_preEvaluateDependencies.SetBool("ignoreSkelDebugInfo", m_pBuildData->m_ignoreSkelDebugInfo);

	// Shared parameters between the reference animation and the regular animation
	m_preEvaluateDependencies.SetInt("sampleRate", m_pCommonData->m_sampleRate);
	m_preEvaluateDependencies.SetBool("isAdditive", m_pCommonData->m_isAdditive);
	m_preEvaluateDependencies.SetBool("isStreaming", m_pCommonData->m_isStreaming);
	m_preEvaluateDependencies.SetBool("isLooping", m_pCommonData->m_isLooping);
	m_preEvaluateDependencies.SetBool("generateCenterOfMass", m_pCommonData->m_generateCenterOfMass);

	m_preEvaluateDependencies.SetInt("skelID", m_pCommonData->m_skelId.GetValue());
 	m_preEvaluateDependencies.SetString("jointCompression", m_pCommonData->m_jointCompression);
 	m_preEvaluateDependencies.SetString("channelCompression", m_pCommonData->m_channelCompression);

	// Add the partial sets
	char keyNameBuffer[1024];
	for (size_t partialSetIndex = 0; partialSetIndex < m_pCommonData->m_partialSets.size(); ++partialSetIndex)
	{
		sprintf(keyNameBuffer, "partialSet%d", partialSetIndex);
		m_preEvaluateDependencies.SetString(keyNameBuffer, m_pCommonData->m_partialSets[partialSetIndex]);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_BuildAnim::Evaluate()
{
	const BuildFile& skelNdbFile = GetInputFile("SkelNdb");
	const BuildFile& rigInfoFile = GetInputFile("RigInfo");
	const BuildFile& animNdbFile = GetInputFile("AnimNdb");

	const BuildFile effNdbFile	   = DoesInputExist("EffNdb") ? GetInputFile("EffNdb") : BuildFile();
	const BuildFile refAnimNdbFile = DoesInputExist("RefAnimNdb") ? GetInputFile("RefAnimNdb") : BuildFile();

	const BuildPath& boPath = GetOutputPath("AnimBo");

	bool animErrorsOccurred = BuildAnim(m_pContext->m_buildScheduler.GetFarmSession(),
										m_pBuildData,
										m_pCommonData,
										skelNdbFile,
										rigInfoFile,
										animNdbFile,
										effNdbFile,
										refAnimNdbFile,
										boPath,
										m_pContext->m_toolParams,
										m_exportCameraCutAnim);

	if (animErrorsOccurred)
	{
		// don't abort until here, so we can at least build all successful anims before dying
		m_depMismatches.push_back("One or more of the animation builds failed.");
		return BuildTransformStatus::kFailed;
	}

	m_pContext->m_buildScheduler.RegisterFarmWaitItem(this, m_pBuildData->m_animBoJobId);

	return BuildTransformStatus::kResumeNeeded;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_BuildAnim::ResumeEvaluation(const SchedulerResumeItem& resumeItem)
{
	return BuildTransformStatus::kOutputsUpdated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void ApplyOptionsToBuildGraph(const BuildTransformContext* pContext,
									 std::vector<const BuildTransform*>& animXforms)
{
	const CommonAnimData* pFirstAnimCommonData = NULL;
	for (auto pConstXform : animXforms)
	{
		BuildTransform* pXform = (BuildTransform*)pConstXform;

		BuildTransform_ExportAnim* pExportXform = NULL;
		BuildTransform_BuildAnim* pBuildXform = NULL;
		const CommonAnimData* pCommonData = NULL;
		if (pXform->GetTypeName() == "ExportAnim")
		{
			pExportXform = (BuildTransform_ExportAnim*)pXform;
			pCommonData = pExportXform->m_pCommonData;

			if (!pFirstAnimCommonData) pFirstAnimCommonData = pCommonData;
		}
		else if (pXform->GetTypeName() == "BuildAnim")
		{
			pBuildXform = (BuildTransform_BuildAnim*)pXform;
			pCommonData = pBuildXform->m_pCommonData;

			if (!pFirstAnimCommonData) pFirstAnimCommonData = pCommonData;
		}

		// If we explicitly specify animations to build on the command line, then only build those
		bool buildSpecificAnim = false;
		bool thisIsTheSpecialAnim = false;
		if (!pContext->m_toolParams.m_animationsToBuild.empty())
		{
			buildSpecificAnim = true;

			for (const string& animToBuild : pContext->m_toolParams.m_animationsToBuild)
			{
				const std::string& animName = pCommonData->m_animName;
				if (animName == animToBuild)
				{
					thisIsTheSpecialAnim = true;
					break;
				}
			}
		}


		//////////////////////////////////////////////////////////////////////////
		// All transforms
		if (buildSpecificAnim)
		{
			if (thisIsTheSpecialAnim)
				pXform->EnableForcedEvaluation();
			else
				pXform->DisableEvaluation();
		}

		//////////////////////////////////////////////////////////////////////////
		// Check ExportAnim transforms
		if (pExportXform)
		{
			// Only do 'bo' files when updating effects
			if (pContext->m_toolParams.m_effectsOnly)
			{
				pXform->DisableEvaluation();
			}
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
static Location WriteAnimGroup(BigStreamWriter& streamWriter,
							   const std::string& animGroupTag,
							   const SkeletonId skelId,
							   const std::vector<std::string>& animTagNames)
{
	const size_t numAnims = animTagNames.size();

	// write AnimGroup item
	BigStreamWriter::Item* pItem = streamWriter.StartItem(BigWriter::ANIM_GROUP, animGroupTag, animGroupTag);

	streamWriter.AddLoginItem(pItem, BigWriter::ANIM_PRIORITY);

	// write struct ArtGroup
	streamWriter.Write4(static_cast<int>(numAnims));
	streamWriter.Write4(skelId.GetValue());
	Location animArrayLoc = streamWriter.WritePointer();

	// Sort the animations by name
	std::vector<std::string> sortedAnims;
	sortedAnims.reserve(numAnims);
	for (size_t i = 0; i < numAnims; ++i)
	{
		sortedAnims.push_back(animTagNames[i]);
	}
	std::sort(sortedAnims.begin(), sortedAnims.end());

	/* write array of ArtItemAnim pointers */
	streamWriter.Align(kAlignDefaultLink);
	streamWriter.SetPointer(animArrayLoc);
	for (int i = 0; i < numAnims; ++i)
	{
		streamWriter.WritePointer(sortedAnims[i].c_str());
	}

	streamWriter.EndItem();

	return pItem->GetItemHdrLocation();
}


/// --------------------------------------------------------------------------------------------------------------- ///
static void WriteAnimGroup(const BuildTransformContext* const pContext,
						   const std::string& actorName,
						   const SkeletonId skelId,
						   const std::vector<std::string>& animNames,
						   const std::vector<std::string>& animTagNames,
						   const BuildPath& animGroupBoPath,
						   bool bLightAnims)
{
	BigStreamWriter stream(pContext->m_toolParams.m_streamConfig);

	const std::string animGroupTag = "misc." + actorName + "." + ToString(skelId.GetValue()) + (bLightAnims ? ".light-anims" : "");		// Here to avoid scoping issues once inserted into the hash table
	if (!animNames.empty())
	{
		ASSERT(animNames.size() == animTagNames.size());
		WriteAnimGroup(stream, animGroupTag, skelId, animTagNames);
	}

	// Write the .bo file.
	NdiBoWriter boWriter(stream);
	boWriter.Write();

	DataStore::WriteData(animGroupBoPath, boWriter.GetMemoryStream());
}


/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_AnimGroup::BuildTransform_AnimGroup(const BuildTransformContext* const pContext,
												   const std::string& actorName,
												   const SkeletonId skelId,
												   const std::vector<std::string>& animNames,
												   const std::vector<std::string>& animTagNames,
												   bool bLightAnims)
	: BuildTransform("AnimGroup", pContext)
	, m_actorName(actorName)
	, m_skelId(skelId)
	, m_animNames(animNames)
	, m_animTagNames(animTagNames)
	, m_bLightAnims(bLightAnims)
{
	m_preEvaluateDependencies.SetBool("lightAnims", bLightAnims);
	for (int i = 0; i < animNames.size(); i++)
	{
		std::stringstream ss;

		ss << "animName" << i;
		const std::string str = ss.str();
		m_preEvaluateDependencies.SetString(str, animNames[i]);
	}

	for (int i = 0; i < animTagNames.size(); i++)
	{
		std::stringstream ss;
		ss << "animTagName" << i;
		const std::string str = ss.str();
		m_preEvaluateDependencies.SetString(str, animTagNames[i]);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_AnimGroup::Evaluate()
{
	const BuildPath animGroupBoPath = GetOutputPath("BoFile");

	WriteAnimGroup(m_pContext, 
		m_actorName, 
		m_skelId,
		m_animNames, 
		m_animTagNames, 
		animGroupBoPath, 
		m_bLightAnims);

	return BuildTransformStatus::kOutputsUpdated;
}


/// --------------------------------------------------------------------------------------------------------------- ///
static bool IsInNameSpace(const std::string& exportNamespace, const std::string& targetPathString)
{
	size_t nspos = targetPathString.find(":", 0);
	if (nspos != std::string::npos)
	{
		std::string targetNamespace = targetPathString.substr(1, nspos - 1);

		size_t pipePos = targetNamespace.find_last_of('|');
		if (pipePos != std::string::npos)
		{
			targetNamespace = targetNamespace.substr(pipePos + 1);
		}

		if (exportNamespace.size() && targetNamespace.compare(exportNamespace) != 0)
		{
			return false;
		}
	}
	return true;
}


/// --------------------------------------------------------------------------------------------------------------- ///
static void ExtractCameraCutSampleIndices(const ITSCENE::SceneDb& animScene,
										  const std::string& animExportNamespace,
										  unsigned sampleRate,
										  std::vector<unsigned>& cameraCuts)
{
	const ITSCENE::JointAnimationList& jointAnimList = animScene.m_bigExt->m_jointUserAnimations;
	for (size_t i = 0; i < jointAnimList.size(); ++i)
	{
		const ITSCENE::JointAnimation* pAnim = jointAnimList[i];

		if (!IsInNameSpace(animExportNamespace, pAnim->TargetPathString()))
			continue;

		std::string targetPathString = pAnim->TargetPathString();
		size_t pos = targetPathString.find("@");
		if (pos != std::string::npos)
		{
			targetPathString = targetPathString.substr(pos + 1, std::string::npos);
		}

		if (targetPathString.find("ndi_camera") != std::string::npos &&
			targetPathString.find(":apReference") == targetPathString.size() - 12)
		{
			ITSCENE::JointChannels jointCh;
			ITSCENE::ChannelsConstant chConstants;
			pAnim->GetSamples(&jointCh, &chConstants);

			// Add logs indicating where the camera cuts are
			for (int channelSampleIndex = 1; channelSampleIndex < jointCh.size(); ++channelSampleIndex)
			{
				const ITSCENE::Channels& prevChannelSample = jointCh[channelSampleIndex - 1];
				const ITSCENE::Channels& currChannelSample = jointCh[channelSampleIndex];
				if (Abs(prevChannelSample.m_s.Z() - currChannelSample.m_s.Z()) > 0.5f)
				{
					INOTE_VERBOSE("Camera cut detected on sample index %d for namespace '%s'\n", channelSampleIndex, animExportNamespace.c_str());

					const float sampleRatio = 30.0f / (float)sampleRate;
					const unsigned frameIndex = channelSampleIndex * sampleRatio;

					cameraCuts.push_back(frameIndex);
				}
			}
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_ValidateCinematicSequence::BuildTransform_ValidateCinematicSequence(const BuildTransformContext* pContext)
	: BuildTransform("ValidateCinSeq", pContext)
{
	SetDependencyMode(DependencyMode::kIgnoreDependency);

	for (int i = 0; i < m_entries.size(); ++i)
	{
		m_preEvaluateDependencies.SetInt("sampleRate-" + ToString(i), m_entries[i].m_sampleRate);
		m_preEvaluateDependencies.SetString("exportNamespace-" + ToString(i), m_entries[i].m_exportNamespace);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_ValidateCinematicSequence::Evaluate()
{
	DataStore::WriteData(GetFirstOutputPath(), "");

	const std::vector<TransformInput>& inputs = GetInputs();

	std::vector<std::vector<unsigned>> allCameraCuts;
	allCameraCuts.resize(inputs.size());

	// Extract all camera cuts
	for (int i = 0; i < inputs.size(); ++i)
	{
		const TransformInput& input = inputs[i];

		ITSCENE::SceneDb animScene;
		NdbStream animNdbStream;
		DataStore::ReadData(input.m_file, animNdbStream);
		bool success = LoadSceneNdb(animNdbStream, &animScene);
		if (!success)
		{
			IABORT("Error processing %s", input.m_file.AsPrefixedPath().c_str());
			return BuildTransformStatus::kFailed;
		}
		else
		{
			ExtractCameraCutSampleIndices(animScene, m_entries[i].m_exportNamespace, m_entries[i].m_sampleRate, allCameraCuts[i]);
		}
	}

	// Now, make sure that all animations have the same number of camera cuts.
	for (int i = 0; i < inputs.size(); ++i)
	{
		// Do all animations have the same number of camera cuts?
		if (allCameraCuts[i].size() != allCameraCuts[0].size())
		{
			size_t pos00 = inputs[0].m_file.AsPrefixedPath().find_last_of('/') + 1;
			size_t pos01 = inputs[0].m_file.AsPrefixedPath().find("-chunk");
			size_t pos10 = inputs[i].m_file.AsPrefixedPath().find_last_of('/') + 1;
			size_t pos11 = inputs[i].m_file.AsPrefixedPath().find("-chunk");
			IWARN("Mismatched camera cut count in '%s' and '%s'",
				  inputs[0].m_file.AsPrefixedPath().substr(pos00, pos01 - pos00).c_str(),
				  inputs[i].m_file.AsPrefixedPath().substr(pos10, pos11 - pos10).c_str());
			//			return BuildTransformStatus::kFailed;
			return BuildTransformStatus::kOutputsUpdated;
		}
	}

	// Now, make sure that all animations have the same camera cuts.
	for (int i = 0; i < inputs.size(); ++i)
	{
		// Do all animations have the same number of camera cuts?
		for (int iCut = 0; iCut < allCameraCuts[0].size(); ++iCut)
		{
			if (allCameraCuts[i][iCut] != allCameraCuts[0][iCut])
			{
				size_t pos00 = inputs[0].m_file.AsPrefixedPath().find_last_of('/') + 1;
				size_t pos01 = inputs[0].m_file.AsPrefixedPath().find("-chunk");
				size_t pos10 = inputs[i].m_file.AsPrefixedPath().find_last_of('/') + 1;
				size_t pos11 = inputs[i].m_file.AsPrefixedPath().find("-chunk");
				IWARN("Mismatched camera cut frames in '%s' and '%s'",
					  inputs[0].m_file.AsPrefixedPath().substr(pos00, pos01 - pos00).c_str(),
					  inputs[i].m_file.AsPrefixedPath().substr(pos10, pos11 - pos10).c_str());
				//				return BuildTransformStatus::kFailed;
				return BuildTransformStatus::kOutputsUpdated;
			}
		}
	}


	return BuildTransformStatus::kOutputsUpdated;
}


/// --------------------------------------------------------------------------------------------------------------- ///
bool BuildModuleAnimation_ConfigureSingleAnimation(const BuildTransformContext* const pContext,
												   const libdb2::Actor* const pDbActor,
												   const libdb2::Anim* const pAnim,
												   const libdb2::Actor* const pActor,
												   std::vector<std::string>& arrBoFiles,
												   std::vector<const BuildTransform*>& animXforms,
												   std::map<SkeletonId, std::set<std::string>>& animNamesPerSkel,
												   std::map<std::string, SkeletonId>& refSkelIds,
												   std::map<std::string, std::string>& refSkelSceneFileNames,
												   std::vector<StreamBoEntry>& streamBoList,
												   BuildTransform_MotionMatching::AnimToDataMap& exportMap,
												   BuildTransform_ExportAnim::InlineEvaluationFunc pExportInlineFunc,
												   BuildTransform_EffGenerator* pEffGenXform,
												   bool ignoreBuildSkelDebugInfo)
{
	const libdb2::Anim* pRefAnim = pAnim->RefAnim();

	// VALIDATION
	// now we need to find the referenced actor to compute the skel id
	std::string skelSceneFile;
	SkeletonId skelId = INVALID_SKELETON_ID;
	{
		// not having an actor ref for an animation is an error....
		if (pAnim->m_actorRef == "")
		{
			IERR("animation %s has no actor referenced (for skeleton).\n", pAnim->FullName().c_str());
			return false;
		}

		if (!pAnim->m_animationStartFrame.m_enabled || !pAnim->m_animationEndFrame.m_enabled)
		{
			IABORT("Animation %s, is missing a start or end frame\n", pAnim->FullName().c_str());
		}

		I32 startFrame = (I32)pAnim->m_animationStartFrame.m_value;
		I32 endFrame = (I32)pAnim->m_animationEndFrame.m_value;
		if (startFrame > endFrame)
		{
			IABORT("Animation start frame comes after end frame for animation '%s'", pAnim->FullName().c_str());
		}

		if (pRefAnim && pRefAnim->Loaded() && pRefAnim->FullName() == pAnim->FullName())
		{
			IABORT("An additive animation cannot be the reference to itself. '%s'", pAnim->FullName().c_str());
		}

		const I32 refAnimStartFrame = (I32)pAnim->m_animationStartFrame.m_value;
		const I32 refAnimEndFrame = (I32)pAnim->m_animationEndFrame.m_value;

		if (pRefAnim && pRefAnim->Loaded() &&
			refAnimStartFrame != refAnimEndFrame &&								// 0 frame ref animations are allowed
			refAnimEndFrame - refAnimStartFrame != endFrame - startFrame)		// non-0 ref anims have to be same length as the additive anim
		{
			IABORT("A reference animation must be the same length as the additive animation OR only have 1 frame. '%s'", pAnim->FullName().c_str());
		}

		if (pAnim->m_flags.m_Streaming && endFrame - startFrame < kNumFramesPerStreamingChunk * 3)
		{
			INOTE_VERBOSE("Trying to make a streaming animation when the animation is too short.  Minimum frame length is %d, animation %s has %d frames.",
						  kNumFramesPerStreamingChunk * 3,
						  pAnim->FullName().c_str(),
						  endFrame - startFrame);

			// Hack, force the animation to be non-streaming
			const_cast<libdb2::Anim*>(pAnim)->m_flags.m_Streaming = false;
		}

		// Cache the skeleton ID and the scene file name
		auto refSkelIdIT = refSkelIds.find(pAnim->m_actorRef);
		auto refSkelSceneFileIT = refSkelSceneFileNames.find(pAnim->m_actorRef);
		if (refSkelIdIT == refSkelIds.end())
		{
			const libdb2::Actor* pSkelActor = libdb2::GetActor(pAnim->m_actorRef);
			if (!pSkelActor->Loaded())
			{
				IERR("Unable to find actor %s used as a ref actor in animation %s", pAnim->m_actorRef.c_str(), pAnim->FullName().c_str());
				return false;
			}
			if (!pSkelActor->m_skeleton.m_exportSkeleton)
			{
				IERR("Actor reference '%s' in animation '%s' does not export a skeleton.\nUpdate the animation to reference an actor that does.", pAnim->m_actorRef.c_str(), pAnim->FullName().c_str());
				return false;
			}
			refSkelIdIT = refSkelIds.insert(std::make_pair(pAnim->m_actorRef, GetSkelId(*pSkelActor))).first;
			refSkelSceneFileIT = refSkelSceneFileNames.insert(std::make_pair(pAnim->m_actorRef, pSkelActor->m_skeleton.m_sceneFile)).first;

//			delete pSkelActor;
		}

		skelSceneFile = refSkelSceneFileIT->second;
		skelId = refSkelIdIT->second;

		if (animNamesPerSkel[skelId].find(pAnim->Name()) != animNamesPerSkel[skelId].end())
		{
			IWARN("Hey MIKE! I Found a duplicate animation '%s', using skeleton %s", pAnim->Name().c_str(), pAnim->m_actorRef.c_str());
			return true;
		}
		animNamesPerSkel[skelId].insert(pAnim->Name());
	}

	// Allow the actor to override the default sample rate and allow the animation to override the actor.
	U32 itemSampleRate = GetAnimSampleRate(*pAnim, pContext->m_toolParams.m_defaultAnimSampleRate);

	AnimExportData* pAnimExportData = new AnimExportData;
	AnimExportData* pRefAnimExportData = NULL;
	if (pRefAnim && pRefAnim->Loaded())
	{
		pRefAnimExportData = new AnimExportData;
	}

	bool errorThisAnim = false;
	std::string skelName;
	GetAnimSkelBoPath(*pAnim, pContext->m_toolParams.m_buildPathSkelBo, skelName, errorThisAnim);

	// Determine the anim export namespace and skel export set
	{
 		const libdb2::Actor* pDbSkelActor = libdb2::GetActor(skelName);

		pAnimExportData->m_skelExportSet = pDbSkelActor->m_skeleton.m_set;
		if (pRefAnimExportData)
			pRefAnimExportData->m_skelExportSet = pDbSkelActor->m_skeleton.m_set;

//		delete pDbSkelActor;
	}

	CommonAnimData* pCommonData = new CommonAnimData;
	pCommonData->m_animName = pAnim->Name();
	pCommonData->m_animFullName = pAnim->FullName();

	// Figure out the animation export namespace
	{
		std::string animExportNamespace = "";
		std::string refAnimExportNamespace = "";
		const libdb2::Actor* pDbSkelActor = libdb2::GetActor(skelName);

		// Default the animation export to the namespace specified in the skeleton 
		if (pDbSkelActor && pDbSkelActor->Loaded() && pDbSkelActor->m_skeleton.Loaded() && pDbSkelActor->m_skeleton.m_defaultAnimExportNamespace.size() > 0)
		{
			animExportNamespace = refAnimExportNamespace = pDbSkelActor->m_skeleton.m_defaultAnimExportNamespace;
		}

		// Allow override of the export namespace on a per-animation basis
		if (pAnim->Loaded() && pAnim->m_exportNamespace.size() > 0)
		{
			animExportNamespace = pAnim->m_exportNamespace;
		}
		if (pRefAnim && pRefAnim->Loaded() && pRefAnim->m_exportNamespace.size() > 0)
		{
			refAnimExportNamespace = pRefAnim->m_exportNamespace;
		}

		// If an animation is defined in the same file as the skeleton, ignore any name spaces
		if (pDbSkelActor->m_skeleton.m_sceneFile == pAnim->m_animationSceneFile)
		{
			animExportNamespace = "";
		}
		if (pRefAnim && pRefAnim->Loaded() && pDbSkelActor->m_skeleton.m_sceneFile == pRefAnim->m_animationSceneFile)
		{
			refAnimExportNamespace = "";
		}

		pCommonData->m_exportNamespace = animExportNamespace;
 		pAnimExportData->m_exportNamespace = animExportNamespace;
		if (pRefAnimExportData)
			pRefAnimExportData->m_exportNamespace = refAnimExportNamespace;
	}

	// Determine the partial sets
	ExportDbManager2::SetsParser setsParser(pAnim->m_sets);
	for (ExportDbManager2::SetsParser::MapRealSet2UserFriendlyName::iterator it = setsParser.m_mapParsedSets.begin(),
		itEnd = setsParser.m_mapParsedSets.end(); it != itEnd; ++it)
	{
		pCommonData->m_partialSets.push_back(it->first);
	}

	// Input
	pAnimExportData->m_startFrame = (I32)pAnim->m_animationStartFrame.m_value;
	pAnimExportData->m_endFrame = (I32)pAnim->m_animationEndFrame.m_value;
	pAnimExportData->m_restrictedStartFrame = pAnimExportData->m_startFrame;
	pAnimExportData->m_restrictedEndFrame = pAnimExportData->m_endFrame;
	pAnimExportData->m_realStartFrame = pAnimExportData->m_startFrame;

	// Dependencies
	size_t pos = pAnim->m_actorRef.find(".actor.xml");
	pCommonData->m_skelSceneFile = skelSceneFile;
	pCommonData->m_skelId = skelId;
	pCommonData->m_skelNdbFilename = pContext->m_toolParams.m_buildPathSkel + pAnim->m_actorRef.substr(0, pos) + FileIO::separator + "skel.ndb";
	pCommonData->m_rigInfoFilename = pContext->m_toolParams.m_buildPathRigInfo + pAnim->m_actorRef.substr(0, pos) + FileIO::separator + "riginfo.ndb";

	pCommonData->m_sampleRate = itemSampleRate;
	pCommonData->m_isAdditive = pRefAnim && pRefAnim->Loaded();
	pCommonData->m_isStreaming = pAnim->m_flags.m_Streaming;
	pCommonData->m_isLooping = pAnim->m_flags.m_Looping;
	pCommonData->m_generateCenterOfMass = pAnim->m_flags.m_CenterOfMass;
	const bool exportCameraCutAnim = pAnim->m_flags.m_exportCameraCutAnim;
	pCommonData->m_singleExport = pContext->m_toolParams.m_singleExport;

	pCommonData->m_jointCompression = pAnim->m_jointCompression;
	pCommonData->m_channelCompression = pAnim->m_channelCompression;

	if (pCommonData->m_isAdditive)
	{
		pRefAnimExportData->m_animFullPath = pRefAnim->FullName() + ".ref-anim";
		pRefAnimExportData->m_startFrame = (I32)pRefAnim->m_animationStartFrame.m_value;
		pRefAnimExportData->m_endFrame = (I32)pRefAnim->m_animationEndFrame.m_value;
		pRefAnimExportData->m_restrictedStartFrame = pRefAnimExportData->m_startFrame;
		pRefAnimExportData->m_restrictedEndFrame = pRefAnimExportData->m_endFrame;
		pRefAnimExportData->m_realStartFrame = pRefAnimExportData->m_startFrame;
		pRefAnimExportData->m_ignorePartialSets = true;
	}

	// Export Output / Build Input
	pAnimExportData->m_animFullPath = pAnim->FullName();

	AnimBuildData* pAnimBuildData = new AnimBuildData(pAnim);
	pAnimBuildData->m_startFrame = 0;
	pAnimBuildData->m_endFrame = pAnimExportData->m_endFrame - pAnimExportData->m_startFrame;
	pAnimBuildData->m_ignoreSkelDebugInfo = ignoreBuildSkelDebugInfo;

	//get the bundle of the animation
	const string& animationFullName = pAnim->DiskPath();
	size_t bundleExtensionPosition = animationFullName.find(".bundle");
	if (bundleExtensionPosition == string::npos)
	{
		IABORT("The animation %s is not in a bundle.", animationFullName.c_str());
	}
	string bundlePath = animationFullName.substr(0, bundleExtensionPosition);
	string bundleDirectory = bundlePath + ".bundle";
	DependencyDataBaseManager::AddDirectoryToBuildDependencies(bundleDirectory.c_str(), "");
	size_t bundleNamePosition = bundlePath.find_last_of('/');
	if (bundleNamePosition == string::npos)
	{
		IABORT("The animation %s is not in a bundle.", animationFullName.c_str());
	}

	string bundleName = bundlePath.substr(bundleNamePosition + 1);

	// Build Output
	const libdb2::Actor* pDbSkeleton = libdb2::GetActor(pAnim->m_actorRef, false);
	const std::string animBoFilename = pContext->m_toolParams.m_buildPathAnimBo + pDbSkeleton->Name() + FileIO::separator + bundleName + FileIO::separator + pAnim->Name() + "." + ToString(pCommonData->m_sampleRate) + "fps.bo";
	pAnimBuildData->m_animBoFilename = animBoFilename;

	const std::string effNdbFilename = (pEffGenXform && !pAnim->m_flags.m_noBoExport) ? pEffGenXform->AddNewEntry(skelName, pAnim->Name()) : std::string();

	if (!effNdbFilename.empty())
	{
		std::string path = EffectMasterTable::GetEffectsPath(skelName, EffectMasterTable::kEffectsTypeArtGroup);
		std::string transformOutput = PathConverter::ToPrefixedPath(effNdbFilename);
		DependencyDataBaseManager::AddDirectoryToBuildDependencies(path.c_str(), transformOutput.c_str());
	}

	// Add the build transforms
	string refAnimationExportFilename;
	if (pRefAnimExportData && !pRefAnim->m_animationSceneFile.empty())
	{
		TransformInput inputFile(PathPrefix::BAM + pRefAnim->m_animationSceneFile);

		refAnimationExportFilename = pContext->m_toolParams.m_buildPathAnim + pDbSkeleton->Name() + FileIO::separator + bundleName + FileIO::separator + pRefAnim->Name() + ".ref-anim." + ToString(itemSampleRate) + "fps.ndb";
		TransformOutput outputFile(refAnimationExportFilename);

		BuildTransform_ExportAnim *const pExportAnim = new BuildTransform_ExportAnim(pContext, 
																					pDbActor->DiskPath(), 
																					pRefAnimExportData, 
																					pCommonData, 
																					pExportInlineFunc);
		pExportAnim->SetInput(inputFile);
		pExportAnim->SetOutput(outputFile);
		pContext->m_buildScheduler.AddBuildTransform(pExportAnim, pContext);
		animXforms.push_back(pExportAnim);
	}

	string animationExportFilename = pContext->m_toolParams.m_buildPathAnim + pDbSkeleton->Name() + FileIO::separator + bundleName + FileIO::separator + pAnim->Name() + "." + ToString(pCommonData->m_sampleRate) + "fps.ndb";
	if (pAnimExportData && !pAnim->m_animationSceneFile.empty())
	{
		TransformInput inputFile(PathPrefix::BAM + pAnim->m_animationSceneFile);
		TransformOutput outputFile(animationExportFilename);

		BuildTransform_ExportAnim *const pExportAnim = new BuildTransform_ExportAnim(pContext, 
																					pDbActor->DiskPath(),
																					pAnimExportData, 
																					pCommonData, 
																					pExportInlineFunc);
		pExportAnim->SetInput(inputFile);
		pExportAnim->SetOutput(outputFile);
		pContext->m_buildScheduler.AddBuildTransform(pExportAnim, pContext);
		animXforms.push_back(pExportAnim);

		exportMap.insert({ pAnim->Name(), MotionMatchingAnimData{ *pCommonData, *pAnimExportData, outputFile } });
	}

	if (!pAnim->m_flags.m_noBoExport)
	{
		std::vector<TransformInput> inputs;
		inputs.push_back(TransformInput(pCommonData->m_skelNdbFilename, "SkelNdb"));
		inputs.push_back(TransformInput(pCommonData->m_rigInfoFilename, "RigInfo"));
		inputs.push_back(TransformInput(animationExportFilename, "AnimNdb"));

		if (!effNdbFilename.empty())
		{
			inputs.push_back(TransformInput(effNdbFilename, "EffNdb"));
		}

		if (pRefAnimExportData)
			inputs.push_back(TransformInput(refAnimationExportFilename, "RefAnimNdb"));
		if (pCommonData->m_isStreaming)
			pAnimBuildData->m_headerOnly = true;

		std::vector<TransformOutput> outputs;
		outputs.push_back(TransformOutput(animBoFilename, "AnimBo"));
		BuildTransform_BuildAnim* pXform = new BuildTransform_BuildAnim(pContext, pAnimBuildData, pCommonData);
		pXform->SetInputs(inputs);
		pXform->SetOutputs(outputs);
		pContext->m_buildScheduler.AddBuildTransform(pXform, pContext);
		animXforms.push_back(pXform);

		arrBoFiles.push_back(animBoFilename);
	}

	if (!ignoreBuildSkelDebugInfo)
	{
		const std::string skelDebugInfoBoFilename = pContext->m_toolParams.m_buildPathSkelDebugInfoBo + pDbSkeleton->Name() + ".skeldebuginfo.bo";
		if (std::find(arrBoFiles.begin(), arrBoFiles.end(), skelDebugInfoBoFilename) == arrBoFiles.end())
		{
			std::vector<TransformInput> inputs;
			inputs.push_back(TransformInput(pCommonData->m_skelNdbFilename, "SkelNdb"));
			inputs.push_back(TransformInput(pCommonData->m_rigInfoFilename, "RigInfo"));

			std::vector<TransformOutput> outputs;
			outputs.push_back(TransformOutput(skelDebugInfoBoFilename, "SkelInfoBo"));
			BuildTransform_BuildDebugSkelInfo* pXform = new BuildTransform_BuildDebugSkelInfo(pContext, skelName);
			pXform->SetInputs(inputs);
			pXform->SetOutputs(outputs);
			pContext->m_buildScheduler.AddBuildTransform(pXform, pContext);

			arrBoFiles.push_back(skelDebugInfoBoFilename);
		}
	}

	// Perform special processing for streaming animations
	if (pAnim->m_flags.m_Streaming && !pAnim->m_flags.m_noBoExport)
	{
		const float startFrame = pAnim->m_animationStartFrame.m_value;
		const float endFrame = pAnim->m_animationEndFrame.m_value;
		const int numChunks = (endFrame - startFrame + kNumFramesPerStreamingChunk - 1) / kNumFramesPerStreamingChunk;

		for (int chunkIndex = 0; chunkIndex < numChunks || chunkIndex == numChunks; chunkIndex++)
		{
			{
				const std::string basePath = animationExportFilename.substr(0, animationExportFilename.length() - 10);
				const std::string endingPath = animationExportFilename.substr(animationExportFilename.length() - 10);

				AnimExportData* pChunkExportData = new AnimExportData(*pAnimExportData);
				string chunkExportFilename;

				if (pCommonData->m_singleExport)
				{
					chunkExportFilename = basePath + "-chunk-all" + endingPath;
					pChunkExportData->m_animFullPath = pChunkExportData->m_animFullPath + "-chunk-all";

					if (chunkIndex == 0)
					{
						// All frames in one export!
						pChunkExportData->m_restrictedStartFrame = pChunkExportData->m_startFrame;
						pChunkExportData->m_restrictedEndFrame = pChunkExportData->m_endFrame;

						TransformInput inputFile(PathPrefix::BAM + pAnim->m_animationSceneFile);
						TransformOutput outputFile(chunkExportFilename);

						BuildTransform_ExportAnim *const pExportAnim = new BuildTransform_ExportAnim(pContext, 
																									pDbActor->Name(),
																									pChunkExportData, 
																									pCommonData, 
																									pExportInlineFunc);
						pExportAnim->SetInput(inputFile);
						pExportAnim->SetOutput(outputFile);
						pContext->m_buildScheduler.AddBuildTransform(pExportAnim, pContext);
						animXforms.push_back(pExportAnim);
					}
				}
				else
				{
					if (chunkIndex == numChunks)
					{
						// Last chunk - only one frame
						pChunkExportData->m_restrictedStartFrame = pChunkExportData->m_endFrame;
						pChunkExportData->m_restrictedEndFrame = pChunkExportData->m_endFrame;

						chunkExportFilename = basePath + "-chunk-last" + endingPath;
						pChunkExportData->m_animFullPath = pChunkExportData->m_animFullPath + "-chunk-last";
					}
					else
					{
						// All other chunks
						const int exportBlockIndex = chunkIndex / kNumStreamingChunksPerExportBlock;
						pChunkExportData->m_restrictedStartFrame = pChunkExportData->m_startFrame + exportBlockIndex * kNumFramesPerStreamingChunk * kNumStreamingChunksPerExportBlock;
						pChunkExportData->m_restrictedEndFrame = pChunkExportData->m_startFrame + (exportBlockIndex + 1) * kNumFramesPerStreamingChunk * kNumStreamingChunksPerExportBlock;

						chunkExportFilename = basePath + "-chunk-" + ToString(chunkIndex / kNumStreamingChunksPerExportBlock) + endingPath;
						pChunkExportData->m_animFullPath = pChunkExportData->m_animFullPath + "-chunk-" + ToString(chunkIndex);
					}

					// Prevent underflow/overflow
					pChunkExportData->m_restrictedStartFrame = MinMax(pChunkExportData->m_restrictedStartFrame, pChunkExportData->m_startFrame, pChunkExportData->m_endFrame);
					pChunkExportData->m_restrictedEndFrame = MinMax(pChunkExportData->m_restrictedEndFrame, pChunkExportData->m_startFrame, pChunkExportData->m_endFrame);

					if ((chunkIndex % kNumStreamingChunksPerExportBlock) == 0 || chunkIndex == numChunks)
					{
						TransformInput inputFile(PathPrefix::BAM + pAnim->m_animationSceneFile);
						TransformOutput outputFile(chunkExportFilename);
						BuildTransform_ExportAnim *const pExportAnim = new BuildTransform_ExportAnim(pContext, 
																									pDbActor->DiskPath(),
																									pChunkExportData, 
																									pCommonData, 
																									pExportInlineFunc);
						pExportAnim->SetInput(inputFile);
						pExportAnim->SetOutput(outputFile);
						pContext->m_buildScheduler.AddBuildTransform(pExportAnim, pContext);
						animXforms.push_back(pExportAnim);
					}
				}

				const int totalFrames = pChunkExportData->m_endFrame - pChunkExportData->m_startFrame;

				AnimBuildData* pStreamingAnimBuildData = new AnimBuildData(pAnim);
				pStreamingAnimBuildData->m_startFrame = 0;
				pStreamingAnimBuildData->m_endFrame = totalFrames;
				pStreamingAnimBuildData->m_ignoreSkelDebugInfo = ignoreBuildSkelDebugInfo;

				// populate the stream anim list for linking into the final art group
				pStreamingAnimBuildData->m_pStreamAnim = new StreamAnim;
				if (chunkIndex == numChunks)
				{
					pStreamingAnimBuildData->m_pStreamAnim->m_fullName = MakeStreamAnimNameFinal(pAnim->FullName());
					pStreamingAnimBuildData->m_pStreamAnim->m_name = MakeStreamAnimNameFinal(pAnim->Name());
					pStreamingAnimBuildData->m_pStreamAnim->m_animStreamChunkIndex = kStreamingChunkFinalIndex;

					pStreamingAnimBuildData->m_startFrame = totalFrames;
					pStreamingAnimBuildData->m_endFrame = totalFrames;
				}
				else
				{
					pStreamingAnimBuildData->m_pStreamAnim->m_fullName = MakeStreamAnimName(pAnim->FullName(), chunkIndex);
					pStreamingAnimBuildData->m_pStreamAnim->m_name = MakeStreamAnimName(pAnim->Name(), chunkIndex);
					pStreamingAnimBuildData->m_pStreamAnim->m_animStreamChunkIndex = chunkIndex;

					const int numFramesInExportBlock = pChunkExportData->m_endFrame - pChunkExportData->m_startFrame;
					const int streamStartFrame = Min(pStreamingAnimBuildData->m_pStreamAnim->m_animStreamChunkIndex * kNumFramesPerStreamingChunk, numFramesInExportBlock);
		  			const int streamEndFrame = Min((pStreamingAnimBuildData->m_pStreamAnim->m_animStreamChunkIndex + 1) * kNumFramesPerStreamingChunk, numFramesInExportBlock);
					pStreamingAnimBuildData->m_startFrame = streamStartFrame;
					pStreamingAnimBuildData->m_endFrame = streamEndFrame;

					// We don't need custom channel data in the stream blocks (except for the last one)
					pStreamingAnimBuildData->m_exportCustomChannels = false;
				}

				const std::string animStreamChunkBoFilename = pContext->m_toolParams.m_buildPathAnimBo
															  + pDbSkeleton->Name() + FileIO::separator + bundleName
															  + FileIO::separator
															  + pStreamingAnimBuildData->m_pStreamAnim->m_name + "."
															  + ToString(pCommonData->m_sampleRate) + "fps.bo";
				pStreamingAnimBuildData->m_animBoFilename = animStreamChunkBoFilename;

				// All animations need to generate a .bo file
				std::vector<TransformInput> inputs;
				inputs.push_back(TransformInput(pCommonData->m_skelNdbFilename, "SkelNdb"));
				inputs.push_back(TransformInput(pCommonData->m_rigInfoFilename, "RigInfo"));
				inputs.push_back(TransformInput(chunkExportFilename, "AnimNdb"));

				if (!effNdbFilename.empty())
				{
					inputs.push_back(TransformInput(effNdbFilename, "EffNdb"));
				}

				std::vector<TransformOutput> outputs;
				outputs.push_back(TransformOutput(animStreamChunkBoFilename, "AnimBo"));
				BuildTransform_BuildAnim* pXform = new BuildTransform_BuildAnim(pContext, pStreamingAnimBuildData, pCommonData);
				pXform->SetInputs(inputs);
				pXform->SetOutputs(outputs);
				pContext->m_buildScheduler.AddBuildTransform(pXform, pContext);
				animXforms.push_back(pXform);

				// Detect camera cuts in this chunk (except for the "last chunk" which is a duplicate of the last frame)
				if (exportCameraCutAnim && chunkIndex != numChunks)
				{
					std::vector<TransformOutput> cameraCutOutputs;
					cameraCutOutputs.push_back(TransformOutput(animStreamChunkBoFilename + ".camera-cut", "AnimBo"));

					BuildTransform_BuildAnim* pCameraCutXform = new BuildTransform_BuildAnim(pContext, pStreamingAnimBuildData, pCommonData, true);
					pCameraCutXform->SetInputs(inputs);
					pCameraCutXform->SetOutputs(cameraCutOutputs);
					pContext->m_buildScheduler.AddBuildTransform(pCameraCutXform, pContext);
					arrBoFiles.push_back(animStreamChunkBoFilename + ".camera-cut");
				}

				// The first and possibly the last chunk goes into the main pak file.
				// All other .bo files will become .pak files and be added to the anim stream pak
				if (chunkIndex == 0 ||			// first chunk
					chunkIndex == numChunks)	// last chunk
				{
					arrBoFiles.push_back(animStreamChunkBoFilename);
				}
				else
				{
					StreamBoEntry entry;
					entry.m_boFileName = animStreamChunkBoFilename;
					entry.m_index = chunkIndex;
					entry.m_streamName = pDbActor->Name() + "." + ConstructStreamName(pStreamingAnimBuildData->m_pDbAnim->Name(), pStreamingAnimBuildData->m_pDbAnim->FullName());
					entry.m_pAnim = pStreamingAnimBuildData->m_pDbAnim;
					entry.m_sampleRate = pCommonData->m_sampleRate;

					streamBoList.push_back(entry);
				}
			}
		}
	}

//	delete pDbSkeleton;

	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildModuleAnimation_ConfigureCommon(const BuildTransformContext* const pContext,
										  const libdb2::Actor* const pDbActor,
										  std::vector<std::string>& arrBoFiles,
										  std::vector<const BuildTransform*>& animXforms,
										  std::vector<StreamBoEntry>& streamBoList,
										  BuildTransform_MotionMatching::AnimToDataMap& exportMap,
										  std::string& actorTxtEntries,
										  bool skipMotionMatching)
{
	if (!skipMotionMatching)	// Skip motion matching for animation reload command, as we only care about the animation bo files.
	{
		// Add the motion matching transform
		std::vector<std::string> mmOutputs = BuildTransform_MotionMatching::CreateTransforms(pContext,
																							 pDbActor,
																							 exportMap);

		arrBoFiles.insert(arrBoFiles.end(), mmOutputs.begin(), mmOutputs.end());
	}

	// Now, allow custom options to enable/disable parts of the build graph
	ApplyOptionsToBuildGraph(pContext, animXforms);

	// Process the streaming chunks
	if (!streamBoList.empty())
	{
		// Sort them based on streamName
		std::sort(streamBoList.begin(), streamBoList.end(), StreamBoCompareFunc);

		std::vector<std::vector<StreamBoEntry>> perStreamBoItems;
		std::vector<int> numSlotsPerStream;
		std::string curAnimName;
		std::vector<StreamBoEntry> streamEntries;
		for (auto entry : streamBoList)
		{
			if (curAnimName != entry.m_streamName)
			{
				if (!streamEntries.empty())
					perStreamBoItems.push_back(streamEntries);
				streamEntries.clear();
				curAnimName = entry.m_streamName;
			}

			streamEntries.push_back(entry);
		}
		perStreamBoItems.push_back(streamEntries);

		// Create all STM files by reading in all bo files, converting them to pak files and then writing them back to back into the STM file
		std::vector<BuildPath> inputStms;
		for (auto streamEntries : perStreamBoItems)
		{
			if (streamEntries.empty())
			{
				continue;
			}

			std::vector<TransformInput> inputs;
			for (const auto& entry : streamEntries)
			{
				inputs.push_back(TransformInput(entry.m_boFileName));
			}

			std::vector<TransformOutput> outputs;
			outputs.push_back(TransformOutput(pContext->m_toolParams.m_buildPathAnimStream + streamEntries[0].m_streamName + ".stm", "AnimStream", TransformOutput::kIncludeInManifest));
			const std::string animStreamBufferBoFilename = pContext->m_toolParams.m_buildPathAnimStream + streamEntries[0].m_streamName + ".bo";
			outputs.push_back(TransformOutput(animStreamBufferBoFilename, "AnimStreamBo"));
			arrBoFiles.push_back(animStreamBufferBoFilename);

			BuildTransform_AnimStreamStm* pStreamXform = new BuildTransform_AnimStreamStm(pContext, streamEntries/*, pStreamBoXform->m_streamHeaders*/);
			pStreamXform->SetInputs(inputs);
			pStreamXform->SetOutputs(outputs);
			pContext->m_buildScheduler.AddBuildTransform(pStreamXform, pContext);

			actorTxtEntries += ("animstream " + streamEntries[0].m_streamName + "\n");
		}
	}

	// Add the anim group transform
	if (!animXforms.empty())
	{
		std::map<SkeletonId, std::vector<std::string>> animNamesPerSkel;
		std::map<SkeletonId, std::vector<std::string>> animTagNamesPerSkel;
		for (size_t itemIndex = 0; itemIndex < animXforms.size(); ++itemIndex)
		{
			const BuildTransform* pBuildXform = animXforms[itemIndex];
			if (pBuildXform->GetTypeName() != "BuildAnim")
				continue;

			const BuildTransform_BuildAnim* pAnimBuildXform = (BuildTransform_BuildAnim*)pBuildXform;
			const AnimBuildData* pBuildData = pAnimBuildXform->m_pBuildData;
			const CommonAnimData* pCommonData = pAnimBuildXform->m_pCommonData;

			if (pBuildData->m_pStreamAnim)
			{
				const bool lastChunk = pBuildData->m_pStreamAnim->m_name.find("-chunk-last") != std::string::npos;

				// We embed the first and the last chunk in the main pak files
				if (pBuildData->m_pStreamAnim->m_animStreamChunkIndex == 0 || lastChunk)
				{
					animNamesPerSkel[pCommonData->m_skelId].push_back(pBuildData->m_pStreamAnim->m_name);

					const libdb2::Actor* pDbSkeleton = libdb2::GetActor(pBuildData->m_pDbAnim->m_actorRef, false);
					string tag = pDbSkeleton->Name() + FileIO::separator + pBuildData->m_pStreamAnim->m_name;
					animTagNamesPerSkel[pCommonData->m_skelId].push_back(tag);

//					delete pDbSkeleton;
				}
			}
			else
			{
				animNamesPerSkel[pCommonData->m_skelId].push_back(pCommonData->m_animName);

				const libdb2::Actor* pDbSkeleton = libdb2::GetActor(pBuildData->m_pDbAnim->m_actorRef, false);
				string tag = pDbSkeleton->Name() + FileIO::separator + pBuildData->m_pDbAnim->Name();
				animTagNamesPerSkel[pCommonData->m_skelId].push_back(tag);

//				delete pDbSkeleton;
			}
		}

		// Create one AnimGroup for each skeleton
		for (auto iter = animNamesPerSkel.begin(); iter != animNamesPerSkel.end(); ++iter)
		{
			BuildTransform_AnimGroup* const pAnimGroup = new BuildTransform_AnimGroup(pContext,
																					  pDbActor->Name(),
																					  iter->first,
																					  animNamesPerSkel[iter->first],
																					  animTagNamesPerSkel[iter->first],
																					  false);

			const std::string animGroupBoPath = pContext->m_toolParams.m_buildPathAnimGroupBo + pDbActor->Name() + "." + ToString(iter->first.GetValue()) + ".bo";
			pAnimGroup->SetOutput(TransformOutput(animGroupBoPath, "BoFile"));
			pContext->m_buildScheduler.AddBuildTransform(pAnimGroup, pContext);
			arrBoFiles.push_back(animGroupBoPath);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Configure all the animations for each actor in the list, this is the path for build.exe
void BuildModuleAnimation_ConfigureFromActorList(const BuildTransformContext *const pContext,
												const libdb2::Actor *const pDbActor, 
												const std::vector<const libdb2::Actor*>& actorList, 
												std::vector<std::string>& arrBoFiles, 
												std::string& actorTxtEntries)
{
//	PushExecutableForFarmUse(kMaya3Ndb, pContext->m_toolParams.m_localexe, pContext->m_toolParams.m_useFarm);
// 	PushExecutableForFarmUse(kAnim3Bo, pContext->m_toolParams.m_localexe, pContext->m_toolParams.m_useFarm);

	std::map<SkeletonId, std::set<std::string>> animNamesPerSkel;	// [validation] animNames per skeleton ID
	std::map<std::string, SkeletonId> refSkelIds;					// [validation] RefActorNames -> skelIds
	std::map<std::string, std::string> refSkelSceneFileNames;		// [validation] RefActorNames -> skelSceneFile
	std::vector<StreamBoEntry> streamBoList;						// All .bo files that go into the stream files
	BuildTransform_MotionMatching::AnimToDataMap exportMap;			// map from anim name to Ndb file for motion matching transform

	BuildTransform_EffGenerator* pEffGenXform = new BuildTransform_EffGenerator(pContext->m_toolParams);
	std::vector<const BuildTransform*> animXforms;

	// Let's gather all animations
	bool abort = false;
	for (const libdb2::Actor* pActor : actorList)
	{
		const libdb2::AnimList& dbanimList = pActor->m_animations;

		for (auto pAnim : dbanimList)
		{
			// skip any disabled animations to save memory
			if (pAnim->m_flags.m_Disabled)
				continue;

			if (!BuildModuleAnimation_ConfigureSingleAnimation(pContext,
															   pDbActor,
															   pAnim,
															   pActor,
															   arrBoFiles,
															   animXforms,
															   animNamesPerSkel,
															   refSkelIds,
															   refSkelSceneFileNames,
															   streamBoList,
															   exportMap,
															   nullptr,
															   pEffGenXform,
															   false))
			{
				abort = true;
			}
		}
	}

	if (abort)
		IABORT("Unable to recover from previous error(s)\n");

	BuildModuleAnimation_ConfigureCommon(pContext,
										 pDbActor,
										 arrBoFiles,
										 animXforms,
										 streamBoList,
										 exportMap,
										 actorTxtEntries,
										 false);

	if (pEffGenXform->HasEntries())
	{
		bool success = pContext->m_buildScheduler.AddBuildTransform(pEffGenXform, pContext);

		if (!success)
		{
			IABORT("failed to add eff xform\n");
		}
	}
	else
	{
		delete pEffGenXform;
		pEffGenXform = nullptr;
	}

	// Add a cinematic validation transform if needed
	if (pContext->GetAssetName().find("seq-") == 0)
	{
		BuildTransform_ValidateCinematicSequence* pValidationXform = new BuildTransform_ValidateCinematicSequence(pContext);

		std::vector<TransformInput> validationInputs;
		for (const auto& pXform : animXforms)
		{
			if (pXform->GetTypeName() != "ExportAnim")
				continue;

			// We only need to look at the first chunk as custom channels are exported for all frames, not just the restricted ones
			if (pXform->GetFirstOutputPath().AsPrefixedPath().find("-chunk-0.") == std::string::npos)
				continue;

			const BuildTransform_ExportAnim* pExportAnimXform = (const BuildTransform_ExportAnim*)pXform;

			// Add optional cinematic sequence validation
			BuildTransform_ValidateCinematicSequence::Entry validationEntry;
			validationEntry.m_exportNamespace = pExportAnimXform->GetPreEvaluateDependencies().GetValue("animExportNamespace");
			validationEntry.m_sampleRate = pExportAnimXform->GetPreEvaluateDependencies().GetInt("sampleRate");
			pValidationXform->m_entries.push_back(validationEntry);
			validationInputs.push_back(TransformInput(pExportAnimXform->GetFirstOutputPath()));
		}

		pValidationXform->SetInputs(validationInputs);
		const std::string dummyFilename = pContext->m_toolParams.m_buildPathCinematicSequenceBo + pContext->GetAssetName() + FileIO::separator + "dummy-output.txt";
		pValidationXform->SetOutput(TransformOutput(dummyFilename, "DummyOutput"));
		pContext->m_buildScheduler.AddBuildTransform(pValidationXform, pContext);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Configure all the animations in a list, this is the path for live updating animations from Maya
void BuildModuleAnimation_ConfigureFromAnimationList(const BuildTransformContext *const pContext,
													const std::vector<const libdb2::Anim*>& animList,
													const libdb2::Actor *const pActor,
													std::vector<std::string>& arrBoFiles,
													std::vector<const BuildTransform*>& animXforms,
													BuildTransform_ExportAnim::InlineEvaluationFunc pInlineFunc, 
													std::string& actorTxtEntries)
{
	//	PushExecutableForFarmUse(kMaya3Ndb, pContext->m_toolParams.m_localexe, pContext->m_toolParams.m_useFarm);
	// 	PushExecutableForFarmUse(kAnim3Bo, pContext->m_toolParams.m_localexe, pContext->m_toolParams.m_useFarm);

	std::map<SkeletonId, std::set<std::string>> animNamesPerSkel;	// [validation] animNames per skeleton ID
	std::map<std::string, SkeletonId> refSkelIds;					// [validation] RefActorNames -> skelIds
	std::map<std::string, std::string> refSkelSceneFileNames;		// [validation] RefActorNames -> skelSceneFile
	std::vector<StreamBoEntry> streamBoList;						// All .bo files that go into the stream files
	BuildTransform_MotionMatching::AnimToDataMap exportMap;			// map from anim name to Ndb file for motion matching transform

	BuildTransform_EffGenerator* pEffGenXform = new BuildTransform_EffGenerator(pContext->m_toolParams);

	for (size_t iAnim = 0; iAnim < animList.size(); ++iAnim)
	{
		const libdb2::Anim* pAnim = animList[iAnim];
		BuildModuleAnimation_ConfigureSingleAnimation(pContext,
													pActor, 
													pAnim, 
													pActor,
													arrBoFiles,
													animXforms,
													animNamesPerSkel,
													refSkelIds,
													refSkelSceneFileNames,
													streamBoList,
													exportMap,
													pInlineFunc,
													pEffGenXform, 
													false);
	}

	BuildModuleAnimation_ConfigureCommon(pContext, 
										pActor, 
										arrBoFiles, 
										animXforms, 
										streamBoList, 
										exportMap, 
										actorTxtEntries, 
										true);

	if (pEffGenXform->HasEntries())
	{
		pContext->m_buildScheduler.AddBuildTransform(pEffGenXform, pContext);
	}
	else
	{
		delete pEffGenXform;
		pEffGenXform = nullptr;
	}
}
