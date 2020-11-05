#include "tools/pipeline3/build/build-transforms/build-transform-parse-actor.h"

#include "tools/pipeline3/build/build-transforms/build-transform-context.h"

#include "tools/pipeline3/build/build-transforms/build-transform-anim-stream.h"
#include "tools/pipeline3/build/build-transforms/build-transform-animation.h"
#include "tools/pipeline3/build/build-transforms/build-transform-cinematic.h"
#include "tools/pipeline3/build/build-transforms/build-transform-collision.h"
#include "tools/pipeline3/build/build-transforms/build-transform-file-list.h"
#include "tools/pipeline3/build/build-transforms/build-transform-geometry-bo.h"
#include "tools/pipeline3/build/build-transforms/build-transform-geometry-export.h"
#include "tools/pipeline3/build/build-transforms/build-transform-geometry.h"
#include "tools/pipeline3/build/build-transforms/build-transform-materials.h"
#include "tools/pipeline3/build/build-transforms/build-transform-pak.h"
#include "tools/pipeline3/build/build-transforms/build-transform-skeleton.h"
#include "tools/pipeline3/build/build-transforms/build-transform-sound-bank-refs.h"
#include "tools/pipeline3/build/build-transforms/build-transform-ui-textures.h"
#include "tools/pipeline3/build/build-transforms/build-transform-widgets.h"
#include "tools/pipeline3/build/build-transforms/build-transform-textures.h"
#include "tools/pipeline3/build/build-transforms/build-transform-actor-runtime-flags.h"
#include "tools/pipeline3/build/build-transforms/build-transform-actor-txt.h"
#include "tools/pipeline3/build/build-transforms/build-transform-light-animation.h"
#include "tools/pipeline3/build/util/build-spec.h"
#include "tools/pipeline3/build/util/dependency-database-manager.h"

#include "tools/pipeline3/toolversion.h"
#include "tools/pipeline3/build/tool-params.h"
#include "tools/pipeline3/common/path-prefix.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/shcomp4/effect_metadata.h"

#include "tools/libs/toolsutil/color-display.h"
#include "tools/libs/toolsutil/strfunc.h"
#include "tools/libs/toolsutil/json_helpers.h"

#include "3rdparty/rapidjson/include/rapidjson/document.h"

using std::string;
using std::vector;

//#pragma optimize("", off) // uncomment when debugging in release mode

//--------------------------------------------------------------------------------------------------------------------//
BuildTransform_ParseActor::BuildTransform_ParseActor(const std::string& actorName
													, const BuildTransformContext *const pContext)
													: BuildTransform("ParseActor", pContext)
													, m_actorName(actorName)
													, m_expandedFiles()
{
	SetDependencyMode(DependencyMode::kIgnoreDependency);
}

//--------------------------------------------------------------------------------------------------------------------//
static void AddLodActors(std::vector<const libdb2::Actor*>& actorList, const libdb2::Actor& baseActor)
{
	// bring int the new style lod actors.
	for (size_t iLod = 1; iLod < baseActor.m_geometryLods.size(); iLod++)  //level0 is the base mesh so we do not include it here
	{
		const libdb2::Actor *const pDbLodActor = libdb2::GetActor(baseActor.Name(), iLod);
		actorList.push_back(pDbLodActor);
	}
}

//--------------------------------------------------------------------------------------------------------------------//
BuildTransformStatus BuildTransform_ParseActor::Evaluate()
{
	const libdb2::Actor *const pDbActor = libdb2::GetActor(m_actorName);

	std::vector<const libdb2::Actor*> actorList;
	
	actorList.push_back(pDbActor);
	AddLodActors(actorList, (*pDbActor));

	if (!LoadXmlFiles(pDbActor, actorList))
	{
		/*for (std::vector<const libdb2::Actor*>::const_iterator iter = actorList.begin(); iter != actorList.end(); iter++)
		{
			delete (*iter);
		}*/
		return BuildTransformStatus::kFailed;
	}

	CreateBuildTransforms(pDbActor, actorList);
	WriteOutputs(actorList);

	AddAssetDependencies(pDbActor);
	AddDiscoveredDependencies(pDbActor);

	/*for (std::vector<const libdb2::Actor*>::const_iterator iter = actorList.begin(); iter != actorList.end(); iter++)
	{
		delete (*iter);
	}*/
	return BuildTransformStatus::kOutputsUpdated;
}

//--------------------------------------------------------------------------------------------------------------------//
static void PopulateRefActorAnimDependencies(const libdb2::Actor* dbactor2, const std::vector<const libdb2::Actor*>& actorList)
{
	// Find all the skeletons referenced by animations built by this actor
	std::vector<std::string> refSkelActorFilenames;
	bool abort = false;
	for (size_t iactor = 0; iactor < actorList.size(); ++iactor)
	{
		const libdb2::Actor* pActor = actorList[iactor];

		const libdb2::AnimList &dbanimList = pActor->m_animations;
		for (const libdb2::Anim *pAnim : dbanimList)
		{
			// skip any disabled animations to save memory
			if (pAnim->m_flags.m_Disabled)
				continue;

			// now we need to find the referenced actor
			auto refSkelActorsIter = std::find(refSkelActorFilenames.begin(), refSkelActorFilenames.end(), pAnim->m_actorRef);
			if (refSkelActorsIter == refSkelActorFilenames.end())
			{
				const libdb2::Actor* skelActor = libdb2::GetActor(pAnim->m_actorRef, false);
				refSkelActorFilenames.push_back(pAnim->m_actorRef);
			}
		}
	}
}


//--------------------------------------------------------------------------------------------------------------------//
bool BuildTransform_ParseActor::LoadXmlFiles(const libdb2::Actor *const pDbActor, 
											std::vector<const libdb2::Actor*>& actorList)
{
	for (libdb2::ActorRefList::const_iterator actorIt = pDbActor->m_subActors.begin(); actorIt != pDbActor->m_subActors.end(); ++actorIt)
	{
		//IABORT((*actorIt).Value().c_str());
		const std::string dependentName  = (*actorIt).GetRefTargetName();
		INOTE_VERBOSE("includes %s", dependentName.c_str());

		const std::string& subactorName = (*actorIt).Value();
		const libdb2::Actor *const pSubactor = libdb2::GetActor(subactorName); //dependentName.c_str());
		if (!pSubactor->Loaded())
		{
			INOTE("\n---------------------------------------------------------------------\n");
			DisplayInfoRed("ERROR \n\n");
			DisplayInfoRed("Unknown actor ");
			DisplayInfoBright("%s\n", dependentName.c_str());
			INOTE("---------------------------------------------------------------------\n");

			return false;
		}
		actorList.push_back(pSubactor);
		AddLodActors(actorList, (*pSubactor));

		{
			for (libdb2::ActorRefList::const_iterator actorIt2 = pSubactor->m_subActors.begin(); actorIt2 != pSubactor->m_subActors.end(); ++actorIt2)
			{
				//IABORT((*actorIt).Value().c_str());
				const std::string dependentName2  = (*actorIt2).GetRefTargetName();
				INOTE_VERBOSE("   includes %s", dependentName2.c_str());

				const std::string& subactorName2 = (*actorIt2).Value();
				const libdb2::Actor *const pSubactor2 = libdb2::GetActor(subactorName2); //dependentName.c_str());
				if (!pSubactor2->Loaded())
				{
					INOTE("\n---------------------------------------------------------------------\n");
					DisplayInfoRed("ERROR \n\n");
					DisplayInfoRed("Unknown actor ");
					DisplayInfoBright("%s\n", dependentName2.c_str());
					INOTE("---------------------------------------------------------------------\n");

					return false;
				}

				U32 i = 0;
				for (; i < actorList.size(); i++)
				{
					if (actorList[i]->FullName() == pSubactor2->FullName())
					{
						break;
					}
				}

				if (i == actorList.size())
				{
					actorList.push_back(pSubactor2);
					AddLodActors(actorList, (*pSubactor2));
				}
			}
		}
	}

	// TO BE REMOVED OR MOVED INTO BUILD_MODULE_ANIM
	// This is ONLY used to populate the dependency table in MySQL.
	PopulateRefActorAnimDependencies(pDbActor, actorList);

	return true;
}


//--------------------------------------------------------------------------------------------------------------------//
static std::string GetRootGui2Path()
{
	return "c:/" + std::string(NdGetEnv("GAMENAME")) + "/data/gui2/";
}


//--------------------------------------------------------------------------------------------------------------------//
void BuildTransform_ParseActor::CreateBuildTransforms(const libdb2::Actor *const pDbActor, 
													const std::vector<const libdb2::Actor*>& actorList)
{
	std::vector<std::string> fileListPaths;
	const bool uiBuild = !pDbActor->m_gui2BuildFile.empty();

	/************************************************************************/
	/* Actor Runtime Flags                                                  */
	/************************************************************************/
	if (!uiBuild)
	{
		std::vector<std::string> boFiles;
		BuildModuleActorRuntimeFlags_Configure(m_pContext, actorList, boFiles);

		if (!boFiles.empty())
		{
			const std::string fileListPath = m_pContext->m_toolParams.m_buildPathFileList + pDbActor->Name() + ".actor-flags.a";
			AddBuildTransform(new BuildTransform_FileList(boFiles, fileListPath));
			fileListPaths.push_back(fileListPath);
		}
	}

	/************************************************************************/
	/* Skeleton                                                             */
	/************************************************************************/
	if (!uiBuild)
	{
		std::vector<std::string> skelBoFiles;
		BuildModuleSkeleton_Configure(m_pContext, 
									pDbActor, 
									actorList, 
									skelBoFiles);

		if (!skelBoFiles.empty())
		{
			const std::string fileListPath = m_pContext->m_toolParams.m_buildPathFileList + pDbActor->Name() + ".skel.a";
			AddBuildTransform(new BuildTransform_FileList(skelBoFiles, fileListPath));
			fileListPaths.push_back(fileListPath);
		}
	}

	/************************************************************************/
	/* Animation                                                            */
	/************************************************************************/
	std::string animActorTxtEntries;
	if (!uiBuild)
	{
		std::vector<std::string> animBoFiles;
		BuildModuleAnimation_ConfigureFromActorList(m_pContext, 
													pDbActor, 
													actorList, 
													animBoFiles, 
													animActorTxtEntries);

		if (!animBoFiles.empty())
		{
			const std::string fileListPath = m_pContext->m_toolParams.m_buildPathFileList + pDbActor->Name() + ".anim.a";
			AddBuildTransform(new BuildTransform_FileList(animBoFiles, fileListPath));
			fileListPaths.push_back(fileListPath);
		}
	}

	/************************************************************************/
	/* Geometry                                                             */
	/************************************************************************/
	std::vector<std::string> geoBoFiles;
	if (!uiBuild)
	{
		BuildModuleGeometryExport_Configure(m_pContext, actorList);
		BuildModuleGeometry_Configure(m_pContext, 
									pDbActor,
									actorList, 
									geoBoFiles);
	}

	/************************************************************************/
	/* Collision                                                            */
	/************************************************************************/
	if (!uiBuild)
	{
		std::vector<std::string> collBoFiles;
		vector<BuildPath> arrToolCollisionPath;
		BuildModuleCollision_Configure(m_pContext, 
									pDbActor, 
									actorList, 
									collBoFiles, 
									arrToolCollisionPath);

		if (!collBoFiles.empty())
		{
			const std::string fileListPath = m_pContext->m_toolParams.m_buildPathFileList + pDbActor->Name() + ".coll.a";
			AddBuildTransform(new BuildTransform_FileList(collBoFiles, fileListPath));
			fileListPaths.push_back(fileListPath);
		}
	}

	/************************************************************************/
	/* Materials                                                            */
	/************************************************************************/
	string consolidatedMaterials;
	if (!uiBuild)
	{
		std::vector<std::string> matBoFiles;
		BuildModuleMaterials_Configure(m_pContext, 
									pDbActor, 
									matBoFiles, 
									consolidatedMaterials, 
									actorList);

		if (!matBoFiles.empty())
		{
			const std::string fileListPath = m_pContext->m_toolParams.m_buildPathFileList + pDbActor->Name() + ".mat.a";
			AddBuildTransform(new BuildTransform_FileList(matBoFiles, fileListPath));
			fileListPaths.push_back(fileListPath);
		}
	}

	/************************************************************************/
	/* Cinematics                                                           */
	/************************************************************************/
	std::vector<std::string> cinematicBoFiles;
	if (!uiBuild)
	{
		BuildModuleCinematic_Configure(m_pContext, 
									pDbActor, 
									cinematicBoFiles);
	}

	/************************************************************************/
	/* Light Anims                                                          */
	/************************************************************************/
	U32 numLightSkels = BuildModuleLightAnim_Configure(m_pContext, pDbActor);

	BigStreamWriter numLightTableStream(m_pContext->m_toolParams.m_streamConfig);
	BigStreamWriter::Item* pItem = numLightTableStream.StartItem(BigWriter::TAG_INT, "num_light_tables");
	numLightTableStream.AddLoginItem(pItem, BigWriter::PRE_LIGHT_PRIORITY);
	numLightTableStream.Write4(numLightSkels);
	numLightTableStream.EndItem();
	numLightTableStream.WriteBoFile(GetOutputPath("NumLightTables"));

	if (numLightSkels > 0)
	{
		// This file could be empty if there's no runtime light in the cinematic/igc
		const std::string lightAnimFileListPath = m_pContext->m_toolParams.m_buildPathFileList + pDbActor->Name() + ".light-anim.a";
		fileListPaths.push_back(lightAnimFileListPath);
	}

	/************************************************************************/
	/* Textures                                                             */
	/************************************************************************/
	string textureBoFilename;
	string textureBoLowFilename;
	string lightBoFilename;
	if (!uiBuild)
	{
		BuildModuleTextures_Configure(m_pContext, 
									pDbActor, 
									actorList, 
									textureBoFilename, 
									textureBoLowFilename, 
									lightBoFilename,
									numLightSkels);
	}

	/************************************************************************/
	/* GUI2 Widgets and Textures                                            */
	/************************************************************************/
	string uiTextureBoFilename;
	string uiTextureBoLowFilename;
	string uiTextureMetadataFilename;
	std::vector<std::string> uiWidgetBos;
	if (uiBuild)
	{
		BuildSpec spec;
		std::string buildFileFullPath = GetRootGui2Path() + pDbActor->m_gui2BuildFile;
		spec.InitFromFile(buildFileFullPath);

		if (spec.IsExtension(".widget"))
		{
			BuildModuleWidgets_Configure(m_pContext, 
										(*pDbActor),
										uiWidgetBos, 
										spec);
		}
		else if (spec.IsExtension(".png"))
		{
			BuildModuleUITextures_Configure(m_pContext, 
										(*pDbActor), 
										uiTextureBoFilename, 
										uiTextureBoLowFilename, 
										uiTextureMetadataFilename, 
										spec, pDbActor->m_gui2Resolutions.m_res);
		}
		else
		{
			IABORT("Build file '%s' specifies an unsupported file extension, or is in some other way corrupt.\n", buildFileFullPath.c_str());
		}
	}

	/************************************************************************/
	/* Sound Banks                                                          */
	/************************************************************************/
	std::vector<std::string> soundBankRefsBoFiles;
	if (!uiBuild)
	{
		BuildTransformSoundBankRefs_Configure(m_pContext, 
											pDbActor, 
											actorList, 
											soundBankRefsBoFiles);
	}

	/************************************************************************/
	/* Geometries                                                            */
	/************************************************************************/

	std::vector<TransformInput> geoBoInputFiles;
	if (!uiBuild)
	{
		if (!consolidatedMaterials.empty())
			geoBoInputFiles.emplace_back(consolidatedMaterials, "consolidatedMaterials");
		BuildModuleGeometryBo_Configure(m_pContext, 
										pDbActor, 
										actorList, 
										geoBoFiles, 
										geoBoInputFiles);
	}

	/************************************************************************/
	/* PAK Files / Linking                                                  */
	/************************************************************************/
	std::vector<std::string> mainPakBoFiles;
	std::vector<std::string> dictPakBoFiles;
	mainPakBoFiles.insert(mainPakBoFiles.end(), geoBoFiles.begin(), geoBoFiles.end());
	mainPakBoFiles.insert(mainPakBoFiles.end(), soundBankRefsBoFiles.begin(), soundBankRefsBoFiles.end());
	mainPakBoFiles.insert(mainPakBoFiles.end(), cinematicBoFiles.begin(), cinematicBoFiles.end());

	mainPakBoFiles.insert(mainPakBoFiles.end(), fileListPaths.begin(), fileListPaths.end());

	// A little hack for now...
	if (!geoBoFiles.empty() || numLightSkels > 0)
	{
		mainPakBoFiles.push_back(textureBoLowFilename);
		dictPakBoFiles.push_back(textureBoFilename);
	}

	if (uiBuild)
	{
		if (uiWidgetBos.empty())
		{
			mainPakBoFiles.push_back(uiTextureBoLowFilename);
			mainPakBoFiles.push_back(uiTextureMetadataFilename);
			//dictPakBoFiles.push_back(uiTextureBoFilename); // we don't generate "high mips" for Gui2 texture dictionaries (they're not mipmapped at all)
		}
		else
		{
			mainPakBoFiles.insert(mainPakBoFiles.end(), uiWidgetBos.begin(), uiWidgetBos.end());
		}
	}

	if (numLightSkels > 0)
		mainPakBoFiles.push_back(lightBoFilename);

	mainPakBoFiles.push_back(GetOutputPath("NumLightTables").AsAbsolutePath());

	//List of pak files
	std::vector<std::string> pakFilenames;

	// Create the main pak file. Always do this, even when there are no bo files (e.g. all-rifles)
	const std::string pakFilename = toolsutils::GetBuildOutputDir() + ACTORFOLDER + FileIO::separator + pDbActor->Name() + ".pak";
	BuildTransformPak_Configure(m_pContext, pakFilename, mainPakBoFiles);
	pakFilenames.push_back(pakFilename);

	// Create the dictionary pak file if needed
	if (!dictPakBoFiles.empty())
	{
		const std::string dictPakFilename = toolsutils::GetBuildOutputDir() + ACTORFOLDER + FileIO::separator + pDbActor->Name() + "-dict" + ".pak";
		BuildTransformPak_Configure(m_pContext, dictPakFilename, dictPakBoFiles, PakHdr::kDontLoadVram);
		pakFilenames.push_back(dictPakFilename);
	}

	BuildTransformActorTxt_Configure(pDbActor, 
									m_pContext, 
									pakFilenames,
									animActorTxtEntries, 
									soundBankRefsBoFiles,
									numLightSkels);
}


//--------------------------------------------------------------------------------------------------------------------//
void BuildTransform_ParseActor::WriteOutputs(const std::vector<const libdb2::Actor*>& actorList)
{

	// Write out all the XML dependencies into the .d file.
	const BuildFile& inputFile = GetFirstInputFile();
	const BuildPath& outputFile = GetFirstOutputPath();

	// Add all the animation/motion-matching Builder XML files 
	for (auto pActor : actorList)
	{
		for (auto anim : pActor->m_animations)
		{
			const auto& animXmlFile = anim->DiskPath();
			RegisterDiscoveredDependency(BuildPath(animXmlFile), 0);
		}

		for (auto& pMotionSet : pActor->m_motionMatchSets)
		{
			const auto& mmXmlFile = pMotionSet->DiskPath();
			RegisterDiscoveredDependency(BuildPath(mmXmlFile), 0);
		}
	}

	// We have no output yet
	DataStore::WriteData(outputFile, "");
}


//--------------------------------------------------------------------------------------------------------------------//
void BuildTransform_ParseActor::AddAssetDependencies(const libdb2::Actor *const pDbActor)
{
	// root actor
	const std::string actorFilename = pDbActor->DiskPath();
	const std::string transformId = GetFirstOutputPath().AsPrefixedPath() + "/" + GetTypeName();

	m_assetDeps.AddAssetDependency(actorFilename, "", "actor");

	ExpandActor(pDbActor);
}


//--------------------------------------------------------------------------------------------------------------------//
void BuildTransform_ParseActor::AddDiscoveredDependencies(const libdb2::Actor *const pDbActor)
{
	const string& gameFlagFilename = pDbActor->m_gameFlagsInfo.m_gameFlagsPath;
	if (!gameFlagFilename.empty())
	{
		BuildPath gameFlagPath(gameFlagFilename);
		RegisterDiscoveredDependency(gameFlagPath, 0);
	}
}


//--------------------------------------------------------------------------------------------------------------------//
void BuildTransform_ParseActor::ExpandActor(const libdb2::Actor *const pActor)
{
	string actorFilename = pActor->DiskPath();

	if (m_expandedFiles.find(actorFilename) == m_expandedFiles.cend())
		m_expandedFiles.insert(actorFilename);
	else
		return;

	const libdb2::AnimList& animationsList = pActor->m_animations;

	string transformId = GetFirstOutputPath().AsPrefixedPath() + "/" + GetTypeName();

	for (auto pAnimation : animationsList)
	{
		const std::string path = pAnimation->DiskPath();
		m_assetDeps.AddAssetDependency(path, actorFilename, "animation");

		//expand animation
		ExpandAnimation(*pAnimation);
	}

	if (!pActor->m_geometry.m_sceneFile.empty())
	{
		const std::string sceneFilename = PathPrefix::BAM + pActor->m_geometry.m_sceneFile;
		m_assetDeps.AddAssetDependency(sceneFilename, actorFilename, "maya");
	}

	if (!pActor->m_skeleton.m_sceneFile.empty())
	{
		const std::string sceneFilename = PathPrefix::BAM + pActor->m_skeleton.m_sceneFile;
		m_assetDeps.AddAssetDependency(sceneFilename, actorFilename, "maya");
	}

	const libdb2::ActorRefList& subActorsList = pActor->m_subActors;
	for (const libdb2::ActorRef& subActorReference : subActorsList)
	{
		string subActorName = subActorReference.GetRefTargetName();
		const libdb2::Actor* pSubActor = libdb2::GetActor(subActorName);

		const std::string path = pSubActor->DiskPath();
		m_assetDeps.AddAssetDependency(path, actorFilename, "actor");

		ExpandActor(pSubActor);
	}
}


//--------------------------------------------------------------------------------------------------------------------//
void BuildTransform_ParseActor::ExpandAnimation(const libdb2::Anim& animation)
{
	string animationPath = animation.DiskPath();

	if (m_expandedFiles.find(animationPath) == m_expandedFiles.cend())
		m_expandedFiles.insert(animationPath);
	else
		return;

	string transformId = GetFirstOutputPath().AsPrefixedPath() + "/" + GetTypeName();

	if (!animation.m_animationSceneFile.empty())
	{
		const std::string sceneFile = PathPrefix::BAM + animation.m_animationSceneFile;
		m_assetDeps.AddAssetDependency(sceneFile, animationPath, "maya");
	}

	const libdb2::Anim* pRefAnim = animation.RefAnim();
	if (pRefAnim && pRefAnim->Loaded())
	{
		const std::string refAnimationFilename = pRefAnim->DiskPath();
		m_assetDeps.AddAssetDependency(refAnimationFilename, animationPath, "animation");

		//expand animation
		ExpandAnimation(*pRefAnim);
	}
//	delete pRefAnim;

	if (!animation.m_actorRef.empty())
	{
		const libdb2::Actor* pSkeletonActor = libdb2::GetActor(animation.m_actorRef);
		if (pSkeletonActor)
		{
			if (pSkeletonActor->Loaded())
			{
				m_assetDeps.AddAssetDependency(pSkeletonActor->DiskPath().c_str(), animationPath.c_str(), "actor");

				ExpandActor(pSkeletonActor);
			}

//			delete pSkeletonActor;
		}
	}
}
