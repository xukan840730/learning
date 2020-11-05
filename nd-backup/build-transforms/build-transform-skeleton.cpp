/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/stdafx.h"
#include "tools/pipeline3/build/build-transforms/build-transform-skeleton.h"

#include "common/imsg/msg_channel_redis.h"
#include "common/hashes/crc32.h"
#include "icelib/ndb/ndbmemorystream.h"
#include "icelib/ndb/ndbmemorystream-helpers.h"
#include "icelib/ndb/ndbwriter.h"
#include "icelib/itscene/ourstudiopolicy.h"
#include "tools/libs/toolsutil/command-job.h"
#include "tools/libs/toolsutil/color-display.h"
#include "tools/libs/toolsutil/simpledb.h"
#include "tools/libs/toolsutil/simple-dependency.h"
#include "tools/libs/exporters/bigexporter.h"
#include "tools/libs/writers/out-riginfo.h"
#include "tools/libs/writers/out-skel.h"
#include "tools/libs/writers/out-flipdata.h"
#include "tools/pipeline3/common/push-to-farm.h"
#include "tools/pipeline3/common/scene-util.h"
#include "tools/pipeline3/common/blobs/blob-cache.h"
#include "tools/pipeline3/common/path-prefix.h"
#include "tools/bamutils/common.h"
#include "tools/bamutils/prjcfg.h"

#include "tools/pipeline3/toolversion.h"

#include "tools/pipeline3/build/tool-params.h"

using toolsutils::SimpleDependency;

//#pragma optimize("", off) // uncomment when debugging in release mode


extern std::string ExtraMaya3NDBOptions(const ToolParams &tool);

//--------------------------------------------------------------------------------------------------------------------//
static void ValidateSetSpelling(const ITSCENE::SceneDb* pScene, const libdb2::Skeleton& skel, const std::string& setName)
{
	const std::string alternateSetName = setName + '_';

	bool setFound = false;
	for (int setIndex = 0; setIndex < pScene->m_sets.size(); ++setIndex)
	{
		ITSCENE::Set* pSet = pScene->m_sets[setIndex];

		const size_t splitPos = pSet->m_name.find_last_of(':');
		const std::string strippedNodeName = (splitPos == std::string::npos) ? pSet->m_name : pSet->m_name.substr(splitPos + 1);

		if (strippedNodeName == setName ||
			strncmp(strippedNodeName.c_str(), alternateSetName.c_str(), alternateSetName.length()) == 0)
		{
			setFound = true;
			break;
		}
	}
	if (!setFound)
	{
		for (int setIndex = 0; setIndex < pScene->m_sets.size(); ++setIndex)
		{
			ITSCENE::Set* pSet = pScene->m_sets[setIndex];

			const size_t splitPos = pSet->m_name.find_last_of(':');
			const std::string strippedNodeName = (splitPos == std::string::npos) ? pSet->m_name : pSet->m_name.substr(splitPos + 1);

			if (strnicmp(strippedNodeName.c_str(), setName.c_str(), setName.length() - 1) == 0)
			{
				IWARN("Set '%s' in the Maya scene for skeleton actor '%s' might be spelled incorrectly. Did you intend for it to be spelled '%s'?", strippedNodeName.c_str(), skel.Name().c_str(), setName.c_str());
				break;
			}
		}
	}
}


//--------------------------------------------------------------------------------------------------------------------//
static void ValidateSets(const ITSCENE::SceneDb* pScene, const libdb2::Skeleton& skel)
{
	if (!skel.m_exportSkeleton || skel.m_sceneFile.empty())
		return;

	// Validate that we have a GameJoints set
	bool gameJointSetFound = false;
	for (int setIndex = 0; setIndex < pScene->m_sets.size(); ++setIndex)
	{
		ITSCENE::Set* pSet = pScene->m_sets[setIndex];

		const size_t splitPos = pSet->m_name.find_last_of(':');
		const std::string strippedNodeName = (splitPos == std::string::npos) ? pSet->m_name : pSet->m_name.substr(splitPos + 1);
		if (strncmp(strippedNodeName.c_str(), "GameJoints", 10) == 0)
		{
			gameJointSetFound = true;
			break;
		}
	}
	if (!gameJointSetFound)
	{
		IERR("A GameJoints set was not found in the Maya scene for skeleton actor '%s'", skel.Name().c_str());
		IABORT("Missing GameJoints set");
	}


	// Validate that all the special sets are named correctly.
	// This is stupid and we shouldn't require exact capitalization but we do for now. :/ - CGY
	ValidateSetSpelling(pScene, skel, "GameJoints");
	ValidateSetSpelling(pScene, skel, "Helpers");
	ValidateSetSpelling(pScene, skel, "AttachPoints");
	ValidateSetSpelling(pScene, skel, "animFlippable");
	ValidateSetSpelling(pScene, skel, "JointSegment");
	ValidateSetSpelling(pScene, skel, "ClothMesh");
	ValidateSetSpelling(pScene, skel, "ClothJoints");
	ValidateSetSpelling(pScene, skel, "PartialAnimSets");
	ValidateSetSpelling(pScene, skel, "blendShape");
	ValidateSetSpelling(pScene, skel, "floatchannel");
	ValidateSetSpelling(pScene, skel, "floatinput");
	ValidateSetSpelling(pScene, skel, "floatoutput");

	// Validate partial sets
	bool allSetsFound = true;
	const std::string& exportSetName = skel.m_set;
	std::set<std::string> partialSetSceneNames;
	for (int bonePairIndex = 0; bonePairIndex < skel.m_bones.size(); ++bonePairIndex)
	{
		const libdb2::Bone& bonePair = skel.m_bones[bonePairIndex];

		bool setFound = false;
		for (int setIndex = 0; setIndex < pScene->m_sets.size(); ++setIndex)
		{
			if (bonePair.m_dst == pScene->m_sets[setIndex]->m_name)
			{
				partialSetSceneNames.insert(bonePair.m_dst);
				setFound = true;
				break;
			}

			// Strip the '_' + export set name if needed
			if (bonePair.m_dst + "_" + exportSetName == pScene->m_sets[setIndex]->m_name)
			{
				partialSetSceneNames.insert(bonePair.m_dst + "_" + exportSetName);
				setFound = true;
				break;
			}
		}
		if (!setFound)
		{
			IERR("Partial Set '%s' not found in scene for skeleton. Please update Maya and/or Builder.", bonePair.m_dst.c_str());
			allSetsFound = false;
		}
	}

	if (!allSetsFound)
	{
		IERR("Not all partial sets were found in the Maya scene for skeleton actor '%s'", skel.Name().c_str());
		IABORT("Missing partial sets");
	}

	// Now validate that all joints and float channels belong to at least one partial set
	if (!partialSetSceneNames.empty())
	{
		for (U32F ii = 0; ii < pScene->GetNumJoints(); ii++)
		{
			const ITSCENE::Joint* pJoint = pScene->GetJoint(ii);
			const std::string& jointDagPath = pJoint->GetFullDagPath();

			// Exclude procedural joints
			if (!g_theStudioPolicy->IsMainJoint(*pScene, jointDagPath))
				continue;

			bool foundInSet = false;
			for (const auto& setName : partialSetSceneNames)
			{
				const ITSCENE::Set* pSet = pScene->GetSet(setName);
				if (pSet->IsMember(jointDagPath))
				{
					foundInSet = true;
					break;
				}
			}

			if (!foundInSet)
			{
				IERR("Joint '%s' not found in any partial set. Please update Maya and/or Builder.", jointDagPath.c_str());
			}
		}

		for (U32F ii = 0; ii < pScene->GetNumFloatAttributes(); ii++)
		{
			const ITSCENE::FloatAttribute* pFloatAttrib = pScene->GetFloatAttribute(ii);
			if (pFloatAttrib->m_typeFlags & ITSCENE::kFaAnimOutput)
			{
				const std::string& pFloatAttribName = pFloatAttrib->GetFullName();

				bool foundInSet = false;
				for (const auto& setName : partialSetSceneNames)
				{
					const ITSCENE::Set* pSet = pScene->GetSet(setName);
					if (pSet->IsMember(pFloatAttribName))
					{
						foundInSet = true;
						break;
					}
				}

				if (!foundInSet)
				{
					IERR("Float Attrib '%s' not found in any partial set. Please update Maya and/or Builder.", pFloatAttribName.c_str());
				}
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------------//
bool BuildTransform_BuildRigInfo::BuildRigInfo(const BuildFile& skelNdbFile,
											   const BuildPath& rigInfoFilename,
											   DataHash& dataHash) const
{
	NdbStream skelNdbStream;
	DataStore::ReadData(skelNdbFile, skelNdbStream);

	ITSCENE::SceneDb *const pScene = new ITSCENE::SceneDb;
	bool bSuccess = LoadSceneNdb(skelNdbStream, pScene);
	if (!bSuccess)
	{
		IABORT("Failed opening up the scene\n");
		return false;
	}

	// Set the studio policy
	BigStudioPolicy aOurStudioPolicy;
	AutoPushPopValue<StudioPolicy*> auto_g_theStudioPolicy(g_theStudioPolicy, &aOurStudioPolicy);

	// Verify that all partial sets actually exist in the scene
	if (m_pDbActor)
	{
		const libdb2::Skeleton *const pDbSkel = &m_pDbActor->m_skeleton;
		ValidateSets(pScene, (*pDbSkel));
	}

	SkeletonId skelID = ComputeSkelId(m_skelSetName);

	BuildPipeline::RigInfoBuilder builder(pScene);
	bSuccess = builder.Build(skelID);
	if (!bSuccess)
	{
//		delete m_pDbActor;
		IABORT("Failed building rig info. Why?!?!?\n");
		return false;
	}

	// Calculate the Hierarchy ID - This is using the information generated
	// in the RigInfo and will generate a new and final joint order in the scene.
	{
		RemovedUnusedShaders(pScene);
		BigExporter::PrepareScene(pScene, &builder);

		std::string value;
		pScene->GetInfo(&value, "hierarchyId");
		U32 hierarchyId = 0;
		if (!sscanf(value.c_str(), "%08x", &hierarchyId))
		{
//			delete m_pDbActor;
			IABORT("prepData version sceneinfo 'hierarchyId' has invalid value '%s'\n", value.c_str());
			return false;
		}

		builder.SetHierarchyId(hierarchyId);
	}

	builder.DumpDetailsToLog();

	NdbStream stream;
	if (stream.OpenForWriting(Ndb::kBinaryStream) != Ndb::kNoError)
	{
//		delete m_pDbActor;
		IABORT("Failed opening output file for rig info. [%s]\n", rigInfoFilename.AsPrefixedPath().c_str());
		return false;
	}

	builder.NdbSerialize(stream);
	stream.Close();

	DataStore::WriteData(BuildPath(rigInfoFilename), stream, &dataHash);

//	delete pScene; // fix me - CGY

//	delete m_pDbActor;
	return bSuccess;
}

void BuildTransform_BuildSkel::PopulatePreEvalDependencies()
{
	m_preEvaluateDependencies.SetConfigString("transformVersion", BUILD_TRANSFORM_SKEL_BUILD_VERSION);

	std::string skelSetName = m_skelSceneFile;
	if (m_skelSet.size())
		skelSetName += "." + m_skelSet;

	SkeletonId uniqueID = ComputeSkelId(skelSetName);
	m_preEvaluateDependencies.SetInt("skelID", uniqueID.GetValue());
}

//--------------------------------------------------------------------------------------------------------------------//
bool BuildTransform_BuildSkel::BuildSkelBo(const ToolParams &tool,
			 BigStreamWriter& streamWriter,
			 std::string& skelCppSourceCode,
			 const BuildFile &skelNdbFile,
			 const BuildFile &rigInfoFile,
			 const BuildPath& skelBoFilename) const
{
	// Set the studio policy
	BigStudioPolicy aOurStudioPolicy;
	AutoPushPopValue<StudioPolicy*> auto_g_theStudioPolicy(g_theStudioPolicy, &aOurStudioPolicy);

	NdbStream skelNdbStream;
	DataStore::ReadData(skelNdbFile, skelNdbStream);

	NdbStream rigInfoStream;
	DataStore::ReadData(rigInfoFile, rigInfoStream);

	BuildPipeline::RigInfo rigInfo;
	rigInfo.NdbSerialize(rigInfoStream);

	INOTE_VERBOSE("SkelFileName = %s\n", skelNdbFile.AsPrefixedPath().c_str());

	ITSCENE::SceneDb* sceneDb = new ITSCENE::SceneDb;
	bool success = LoadSceneNdb(skelNdbStream, sceneDb);
	if (!success)
	{
		IABORT("Error processing %s", skelNdbFile.AsPrefixedPath().c_str());
		return false;
	}

	RemovedUnusedShaders(sceneDb);
	BigExporter::PrepareScene(sceneDb, &rigInfo);

	if (sceneDb->m_joints.size() != rigInfo.NumJoints())
	{
		IABORT("Unmatched scene (%d joints) and riginfo (%d joints).", sceneDb->m_joints.size(), rigInfo.NumJoints());
		return false;
	}

	std::string uniqueName = std::string("skel.") + m_skelFullName;

	BuildPipeline::SkelInfo skelInfo(sceneDb, &rigInfo, uniqueName, m_skelSet);

	if (m_skeletonName.empty())
	{
		IABORT("Actor name cannot be empty when exporting skeletons\n");
	}

	std::string tag = std::string("skel.") + m_skeletonName;

	{
		std::string skelSetName = m_skelSceneFile;
		if (m_skelSet.size())
			skelSetName += "." + m_skelSet;

		SkeletonId uniqueID = ComputeSkelId(skelSetName);
		INOTE_VERBOSE("          skel id = 0x%08X (%s)", uniqueID.GetValue(), skelSetName.c_str());
		skelInfo.WriteSkelBo(streamWriter, skelCppSourceCode, tag, tag, uniqueID.GetValue());

		std::string value;
		if (!sceneDb->GetInfo(&value, "hierarchyId")) 
		{
			IABORT("'hierarchyId' not found\n");
		}
		else
		{
			SimpleDB::Set(skelBoFilename.AsAbsolutePath() + ".hierarchyId", value);
			INOTE_VERBOSE("Skel HierarchyId %s", value.c_str());
		}

		// compute the hash of the used joints in that skeleton for dependency purpose...
		U32 jointsNameCrc = 0;
		for (U32 iJoint = 0; iJoint < rigInfo.NumJoints(); ++iJoint)
		{
			INOTE_VERBOSE("Joint %d = %s", iJoint, rigInfo.GetJoint(iJoint).m_name.c_str());
			jointsNameCrc = StrCrc32(rigInfo.GetJoint(iJoint).m_name.c_str(), jointsNameCrc);

		}
		std::ostringstream jointsNameCRCStr;
		jointsNameCRCStr<<jointsNameCrc;
		SimpleDB::Set(skelBoFilename.AsAbsolutePath() + ".jointHash", jointsNameCRCStr.str());
	}

//	delete sceneDb; // fix me - CGY

	return true;
}


//--------------------------------------------------------------------------------------------------------------------//
bool BuildTransform_BuildSkel::BuildFlipData(const ToolParams &tool,
					   BigStreamWriter& streamWriter,
					   const BuildFile &rigInfoFilename) const
{
	// Open input NDB

	NdbStream rigInfoStream;
	DataStore::ReadData(rigInfoFilename, rigInfoStream);

	// Read the rig info
	BuildPipeline::RigInfo riginfo;
	riginfo.NdbSerialize(rigInfoStream); // read the riginfo

	if(riginfo.IsEmpty())
	{
		IABORT("Cannot read riginfo or empty riginfo file.\n");
		return false;
	}

	INOTE_VERBOSE("Building flipdata");
	BuildPipeline::FlipDataBuilder flipdata(&riginfo);
	if (!flipdata.Build())
	{
		IABORT("Cannot build flipdata!\n");
		return false;
	}

	std::string uniqueName = std::string("skel.") + m_skelFullName;
	//if (actorName.size()) uniqueName += "." + actorName;
	INOTE_VERBOSE(uniqueName.c_str());
	SkeletonId uniqueID = ComputeSkelId(uniqueName);
	INOTE(IMsg::kVerbose, "          flip skel id = 0x%08X (%s)", uniqueID.GetValue(), uniqueName.c_str());

	flipdata.Write(streamWriter, uniqueName);

	return true;
}

void BuildTransform_ExportSkel::PopulatePreEvalDependencies()
{
	m_preEvaluateDependencies.SetConfigString("transformVersion", BUILD_TRANSFORM_SKEL_EXPORT_VERSION);

	if (m_pContext->m_toolParams.m_baseMayaVer.size())
		m_preEvaluateDependencies.SetString("mayaVersion", m_pContext->m_toolParams.m_baseMayaVer);

	m_preEvaluateDependencies.SetString("skelExportNamespace", m_skelExportNamespace);
	m_preEvaluateDependencies.SetString("skelExportSet", m_skelExportSet);

	m_preEvaluateDependencies.SetBool("fullRigExport", m_pContext->m_toolParams.m_fullRig);			// We need to differentiate between using the reference skeleton and using the actual referenced scene file
}


BuildTransformStatus BuildTransform_ExportSkel::Evaluate()
{
	const BuildFile inputSceneFile = GetFirstInputFile();
	const BuildPath skelExportFilename = GetFirstOutputPath();

	std::string options = "";
	if (m_pContext->m_toolParams.m_local)
		options += "-local ";
	options += inputSceneFile.AsRelativePath() + " ";
	options += "-output " + skelExportFilename.AsAbsolutePath() + " -skel";
	if (!m_skelExportSet.empty())
	{
		options += " -skelset " + m_skelExportSet;
	}
	if (!m_skelExportNamespace.empty())
	{
		// Pass the -namespace argument, which enables the "load only the necessary referenced .ma files" optimization.
		// NB: If we can't determine the namespace, we MUST load all referenced rigs or we'll get nada!
		options += " -skelnamespace " + m_skelExportNamespace;
	}
	options += " -user " + m_pContext->m_toolParams.m_userName;

	std::string dbPath = toolsutils::GetGlobalDbPath(m_pContext->m_toolParams.m_host);
	options += " -dbpath " + dbPath;
	options += " -xmlfile " + m_skeletonFullName;
	options += ExtraMaya3NDBOptions(m_pContext->m_toolParams);

	options += " -verbose ";

	const auto& tool = m_pContext->m_toolParams;

	IASSERT(!tool.m_executablesPath.empty());
	const std::string exePath = tool.m_executablesPath + "/maya3ndb.app/" + "maya3ndb.exe";

	JobDescription job(exePath, options,tool.m_localtools, tool.m_local, tool.m_x64maya);
	job.m_useSetCmdLine = true;

	m_jobId = m_pContext->m_buildScheduler.GetFarmSession().submitJob(job.GetCommand(), 800ULL * 1024ULL * 1024ULL, 1);  //default on the farm agent is 400 megs
	m_pContext->m_buildScheduler.RegisterFarmWaitItem(this, m_jobId);
	return BuildTransformStatus::kResumeNeeded;
}

bool FindReferencedSkeletons(const std::vector<const libdb2::Actor*>& actorList, std::vector<std::string>& outSomeActors)
{
	// Find all the skeletons referenced by animations built by this actor
	bool abort = false;
	for (size_t iactor = 0; iactor < actorList.size(); ++iactor)
	{
		const libdb2::Actor* pActor = actorList[iactor];

		// Skeletons are only allowed to be exported from a set.
		if (!pActor->m_skeleton.m_sceneFile.empty() && pActor->m_skeleton.m_exportSkeleton && pActor->m_skeleton.m_set.empty())
		{
			IERR("Skeleton in actor '%s' is exported without using a set. Update the Maya scene file and add the set name in Builder.\n", pActor->Name().c_str());
			abort = true;
			continue;
		}

		const libdb2::AnimList &dbanimList = pActor->m_animations;
		for (const libdb2::Anim* pAnim : dbanimList)
		{
			

			// skip any disabled animations to save memory
			if (pAnim->m_flags.m_Disabled)
				continue;

			// not having an actor ref for an animation is an error....
			if (pAnim->m_actorRef == "")
			{
				IERR("animation %s has no actor referenced (for skeleton).\n", pAnim->FullName().c_str());
				abort = true;
				continue;
			}

			// now we need to find the referenced actor
			auto refSkelActorsIter = std::find(outSomeActors.begin(), outSomeActors.end(), pAnim->m_actorRef);
			if (refSkelActorsIter == outSomeActors.end())
			{
				const libdb2::Actor* skelActor = libdb2::GetActor(pAnim->m_actorRef, false);
				if (!skelActor->Loaded())
				{
					IERR("Unable to find actor %s used as a ref actor in animation %s", pAnim->m_actorRef.c_str(), pAnim->FullName().c_str());
					abort = true;
					continue;
				}
				if (!skelActor->m_skeleton.m_exportSkeleton)
				{
					IERR("Actor reference '%s' in animation '%s' does not export a skeleton.\nUpdate the animation to reference an actor that does.", pAnim->m_actorRef.c_str(), pAnim->FullName().c_str());
					abort = true;
					continue;
				}

				outSomeActors.push_back(pAnim->m_actorRef);
			}
		}
	}

	if (abort)
		return false;

	return true;
}

bool GatherDependentSkeletons(std::vector<const libdb2::Actor*>& outRefActors, const std::vector<std::string>& refActorNames, const libdb2::Actor *dbactor2, const ToolParams& tool)
{
	// Gather all the skeletons that need to be exported
	for (std::vector<std::string>::const_iterator it = refActorNames.begin(), itEnd = refActorNames.end(); it != itEnd; ++it)
	{
		const libdb2::Actor* pDbrefactor2 = libdb2::GetActor(*it, false);
		if (!pDbrefactor2->Loaded())
		{
			IABORT("Unknown Actor '%s'\n", (*it).c_str());
			return false;
		}

		INOTE_VERBOSE("RefActor %s", pDbrefactor2->Name().c_str());
		if (!pDbrefactor2->m_skeleton.Loaded())
			continue; // no skeleton to export.

		// If this actor uses the same skeleton as the one passed on the cmd line, no need to export it again.
		if ((!dbactor2->m_skeleton.Loaded()) ||
			(pDbrefactor2->m_skeleton.FullName().compare(dbactor2->m_skeleton.FullName()) != 0))
		{
			outRefActors.push_back(pDbrefactor2);
		}
	}

	return true;
}

//--------------------------------------------------------------------------------------------------------------------//
std::string RemapDeformationRigFileName(const std::string& skeletonFileName)
{
	// We replace the 'deformation_rig' with 'reference_skeleton'. 
	std::string filename = skeletonFileName;
	size_t pos = filename.find("deformation_rig.ma");
	if (pos != std::string::npos && skeletonFileName.find("master_deformation_rig.ma") == std::string::npos)
		filename = filename.substr(0, pos) + "reference_skeleton.ma";

	return filename;
}

//--------------------------------------------------------------------------------------------------------------------//
void BuildModuleSkeleton_Configure(const BuildTransformContext *const pContext, 
								const libdb2::Actor *const pDbActor, 
								const std::vector<const libdb2::Actor*>& actorList, 
								std::vector<std::string>& arrBoFiles)
{
	// Find all the skeletons referenced by animations built by this actor
	std::vector<std::string> refSkelActorFilenames;
	FindReferencedSkeletons(actorList, refSkelActorFilenames);
	
	// Export and build the skeletons for actors referenced by the actor on the cmd line
	std::vector<const libdb2::Actor*> refActors;
	if (!GatherDependentSkeletons(refActors, refSkelActorFilenames, pDbActor, pContext->m_toolParams))
	{
		IABORT(false);
	}

	// Figure out all the input and output files
	std::vector<std::string> addedSkelItemNames;
	for (size_t actorLoop = 0; actorLoop < actorList.size(); actorLoop++)
	{
		const libdb2::Actor *dbactor = actorList[actorLoop];
		if (dbactor->m_skeleton.Loaded() && dbactor->m_skeleton.m_sceneFile.size())
		{
			// Prevent actor LODs from adding duplicate entries
			const std::string fullNameNoLod = dbactor->FullNameNoLod();
			if (std::find(addedSkelItemNames.begin(), addedSkelItemNames.end(), fullNameNoLod) != addedSkelItemNames.end())
				continue;

			addedSkelItemNames.push_back(fullNameNoLod);

			const std::string skelName = dbactor->Name();

			// Export Output / Build Input
			const std::string skelExportFilename = pContext->m_toolParams.m_buildPathSkel + dbactor->FullNameNoLod() + FileIO::separator + "skel.ndb";

			// Build Output
			const std::string rigInfoFilename = pContext->m_toolParams.m_buildPathRigInfo + dbactor->FullNameNoLod() + FileIO::separator + "riginfo.ndb";
			const std::string skelBoFilename = pContext->m_toolParams.m_buildPathSkelBo + dbactor->FullNameNoLod() + FileIO::separator + "skel.bo";
			const std::string skelCppFilename = pContext->m_toolParams.m_buildPathSkelBo + dbactor->Name() + ".skel.cpp";

			if (dbactor->m_skeleton.m_exportSkeleton)
			{
				arrBoFiles.push_back(skelBoFilename);
			}

			// Add the transforms
			{
				std::vector<TransformInput> inputs;
				inputs.push_back(TransformInput(PathPrefix::BAM + RemapDeformationRigFileName(dbactor->m_skeleton.m_sceneFile)));
				
				TransformOutput outputFile(skelExportFilename);
				BuildTransform_ExportSkel* pXform = new BuildTransform_ExportSkel(pContext, dbactor->FullNameNoLod(), dbactor->m_skeleton.m_skelExportNamespace, dbactor->m_skeleton.m_set);
				pXform->m_skeletonName = skelName;
				pXform->m_skeletonFullName = dbactor->m_skeleton.FullName();
				pXform->SetInputs(inputs);
				pXform->SetOutput(outputFile);
				pContext->m_buildScheduler.AddBuildTransform(pXform, pContext);
			}

			{
				std::vector<TransformInput> inputs;
				inputs.push_back(TransformInput(skelExportFilename, "SkelNdb"));

				std::vector<TransformOutput> outputs;
				outputs.push_back(TransformOutput(rigInfoFilename, "RigInfo"));
				BuildTransform_BuildRigInfo *const pBuildRigInfo = new BuildTransform_BuildRigInfo(pContext, 
																					dbactor->BaseName(), 
																					dbactor->m_lodIndex);
				pBuildRigInfo->m_skeletonName = skelName;
				pBuildRigInfo->SetInputs(inputs);
				pBuildRigInfo->SetOutputs(outputs);
				pContext->m_buildScheduler.AddBuildTransform(pBuildRigInfo, pContext);
			}

			{
				std::vector<TransformInput> inputs;
				inputs.push_back(TransformInput(skelExportFilename, "SkelNdb"));
				inputs.push_back(TransformInput(rigInfoFilename, "RigInfo"));

				std::vector<TransformOutput> outputs;
				outputs.push_back(TransformOutput(skelBoFilename, "SkelBo"));
				outputs.push_back(TransformOutput(skelCppFilename, "SkelCpp" /*, TransformOutput::kReplicate */));
				BuildTransform_BuildSkel* pXform = new BuildTransform_BuildSkel(pContext, dbactor->m_skeleton.m_sceneFile, dbactor->m_skeleton.m_set, dbactor->m_skeleton.FullName());
				pXform->m_skeletonName = skelName;
				pXform->SetInputs(inputs);
				pXform->SetOutputs(outputs);
				pContext->m_buildScheduler.AddBuildTransform(pXform, pContext);
			}
		}
	}

	// Add skeletons referenced by animations
	for (size_t actorLoop = 0; actorLoop < refActors.size(); actorLoop++)
	{
		const libdb2::Actor *dbactor = refActors[actorLoop];
		if (dbactor->m_skeleton.Loaded() && dbactor->m_skeleton.m_sceneFile.size())
		{
			const std::string skelName = dbactor->Name();

			// Export Output
			const std::string skelExportFilename = pContext->m_toolParams.m_buildPathSkel + dbactor->FullNameNoLod() + FileIO::separator + "skel.ndb";

			// Build Output
			const std::string rigInfoFilename = pContext->m_toolParams.m_buildPathRigInfo + dbactor->FullNameNoLod() + FileIO::separator + "riginfo.ndb";
			const std::string skelBoFilename = pContext->m_toolParams.m_buildPathSkelBo + dbactor->FullNameNoLod() + FileIO::separator + "skel.bo";
			const std::string skelCppFilename = pContext->m_toolParams.m_buildPathSkelBo + dbactor->Name() + ".skel.cpp";


			// Add the build transforms
			{
				std::vector<TransformInput> inputs;
				inputs.push_back(TransformInput(PathPrefix::BAM + RemapDeformationRigFileName(dbactor->m_skeleton.m_sceneFile)));

				TransformOutput outputFile(skelExportFilename);
				BuildTransform_ExportSkel* pXform = new BuildTransform_ExportSkel(pContext, dbactor->FullNameNoLod(), dbactor->m_skeleton.m_skelExportNamespace, dbactor->m_skeleton.m_set);
				pXform->m_skeletonName = skelName;
				pXform->m_skeletonFullName = dbactor->m_skeleton.FullName();
				pXform->SetInputs(inputs);
				pXform->SetOutput(outputFile);
				pContext->m_buildScheduler.AddBuildTransform(pXform, pContext);
			}

			{
				std::vector<TransformInput> inputs;
				inputs.push_back(TransformInput(skelExportFilename, "SkelNdb"));

				std::vector<TransformOutput> outputs;
				outputs.push_back(TransformOutput(rigInfoFilename, "RigInfo"));
				BuildTransform_BuildRigInfo *const pBuildRigInfo = new BuildTransform_BuildRigInfo(pContext, 
																								dbactor->BaseName(), 
																								dbactor->m_lodIndex);
				pBuildRigInfo->m_skeletonName = skelName;
				pBuildRigInfo->SetInputs(inputs);
				pBuildRigInfo->SetOutputs(outputs);
				pContext->m_buildScheduler.AddBuildTransform(pBuildRigInfo, pContext);
			}
		}
	}
}
