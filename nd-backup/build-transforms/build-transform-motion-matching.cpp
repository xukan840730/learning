/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "tools/pipeline3/build/stdafx.h"
#include "tools/pipeline3/build/build-transforms/build-transform-motion-matching.h"

#include "common/imsg/msg_channel_redis.h"

#include "tools/libs/bigstreamwriter/ndi-bo-writer.h"
#include "tools/libs/toolsutil/color-display.h"
#include "tools/libs/toolsutil/filename.h"
#include "tools/libs/toolsutil/ndbserialize.h"

#include "tools/pipeline3/build/tool-params.h"
#include "tools/pipeline3/common/anim-util.h"
#include "tools/pipeline3/common/blobs/data-store.h"
#include "tools/pipeline3/common/kmnn-index.h"
#include "tools/pipeline3/toolversion.h"

#include <Eigen/Dense>
#include <unordered_set>

//#define DEBUG_MOTION_MATCHING 1
#define MM_HYBRID_DIST_FUNC 0

// #define DEBUG_MM_ANIM "dina-mm-ambi-idle^move-180l^walk-fw-l-foot"

#ifdef DEBUG_MM_ANIM
#pragma optimize("", off)
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
using AnimVector = Eigen::Matrix<float, Eigen::Dynamic, 1>;
using AnimVectorArray = std::vector<AnimVector>;
using AnimIndicesMap = std::unordered_map<const libdb2::Anim*, std::vector<int>>;
using AnimPointers = std::vector<const libdb2::Anim*>;

static const size_t kNumGoalEntriesPerSample = 10; /* 3 for pos + 3 for vel + 3 for face + 1 for yaw speed */
static const size_t kNumGoalEntriesPerGoalLoc = 3; /* just 3 for pos for now */

struct AnimVectorIndex
{
	int m_iEntry;
};

using AnimVectorIndexArray = std::vector<AnimVectorIndex>;
using AnimVectorIndexMap = std::unordered_map<std::string, AnimVectorIndexArray>;

/// --------------------------------------------------------------------------------------------------------------- ///
class AnimVectorSubset
{
public:
	AnimVectorSubset(const AnimVectorIndexArray& indices, const AnimVectorArray& srcTable)
		: m_indices(indices), m_srcTable(srcTable)
	{
	}

	size_t size() const { return m_indices.size(); }

	//AnimVector& operator[](int i) { return at(i); }
	const AnimVector& operator[](int i) const { return at(i); }

	const AnimVector& at(int i) const
	{
		const size_t iEntry = TranslateIndex(i);
		return m_srcTable[iEntry];
	}

/*
	AnimVector& at(int i)
	{
		IABORT_IF(i < 0 || i >= m_indices.size(), "AnimVectorSubset access out of bounds %d (max %d)", i, size());
		return m_srcTable[m_indices[i].m_iEntry];
	}*/

	const int TranslateIndex(int i) const
	{
		IABORT_IF(i < 0 || i >= m_indices.size(), "AnimVectorSubset access out of bounds %d (max %d)", i, size());
		return m_indices[i].m_iEntry;
	}

	const AnimVectorArray& GetSrcTable() const { return m_srcTable; }

private:
	AnimVectorIndexArray m_indices;
	const AnimVectorArray& m_srcTable;
};

/// --------------------------------------------------------------------------------------------------------------- ///
// The metric function and the way its parameters are computed needs to match the runtime versions as well for the 
// indices to be valid
/// --------------------------------------------------------------------------------------------------------------- ///
class MotionMatchMetric
{
public:
	using DistType = float;

	MotionMatchMetric(const AnimVector& scales, const AnimVector& min)
		: m_scale(scales), m_min(min)
	{
	}

	float Dist(const AnimVector& a, const AnimVector& b) const
	{
		const AnimVector zero = AnimVector::Zero(a.rows());
		const AnimVector absDiff = ((a - b).cwiseAbs() - m_min).cwiseMax(zero);
		const AnimVector scaled = absDiff.cwiseProduct(m_scale);

#if MM_HYBRID_DIST_FUNC
		const float distSqr = scaled.squaredNorm();
		const float distLinear = Sqrt(distSqr);
		const float dist = Min(distSqr, distLinear);
#else
		const float dist = scaled.norm();
#endif

		return dist;
	}

	const AnimVector& GetScaleVector() const { return m_scale; }
	const AnimVector& GetMinimumVector() const { return m_min; }

private:
	AnimVector m_min;
	AnimVector m_scale;
};

/// --------------------------------------------------------------------------------------------------------------- ///
struct MetricVectors
{
	AnimVector m_scale;
	AnimVector m_minimum;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class MotionMatchIndex : public KMNNIndex<AnimVector, MotionMatchMetric, AnimVectorSubset>
{
public:
	typedef KMNNIndex<AnimVector, MotionMatchMetric, AnimVectorSubset> ParentClass;

	MotionMatchIndex(const std::string name,
					 const AnimVectorSubset& vectors,
					 const AnimVector& scales,
					 const AnimVector& mins)
		: ParentClass(MotionMatchMetric(scales, mins)), m_name(name), m_vectors(vectors)
	{
	}

	const std::string& GetName() const { return m_name; }

	const int TranslateIndex(int i) const
	{
		return m_vectors.TranslateIndex(i);
	}

private:
	std::string m_name;
	AnimVectorSubset m_vectors;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static inline bool IsFinite(float a)
{
	return (*PunPtr<U32*>(&a) & 0x7F800000U) != 0x7F800000U;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool LoadSceneNdb(const BuildFile& sceneFile, ITSCENE::SceneDb* scene)
{
	NdbStream sceneNdbStream;
	DataStore::ReadData(sceneFile, sceneNdbStream);
	return LoadSceneNdb(sceneNdbStream, scene);
}

/// --------------------------------------------------------------------------------------------------------------- ///
static BuildPipeline::RigInfo LoadRigInfo(const std::string& filename)
{
	INOTE_VERBOSE("Loading RigInfo: %s\n", filename.c_str());
	NdbStream streamRigInfo;
	if (Ndb::kNoError != streamRigInfo.OpenForReading(filename.c_str(), Ndb::kBinaryStream))
	{
		IERR("Cannot open the riginfo file %s", filename.c_str());
		return BuildPipeline::RigInfo();
	}

	BuildPipeline::RigInfo rigInfo;
	rigInfo.NdbSerialize(streamRigInfo);
	return rigInfo;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static BuildPipeline::RigInfo LoadRigInfo(const BuildFile& rigFile)
{
	INOTE_VERBOSE("Loading RigInfo: %s\n", rigFile.AsAbsolutePath().c_str());
	NdbStream streamRigInfo;
	DataStore::ReadData(rigFile, streamRigInfo);

	BuildPipeline::RigInfo rigInfo;
	rigInfo.NdbSerialize(streamRigInfo);
	return rigInfo;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void BakeAnimScale(ITSCENE::SceneDb& animScene, const ITSCENE::SceneDb& skelScene)
{
	const size_t numJointAnims	   = animScene.GetNumJointAnimations();
	ITSCENE::JointAnimation* jroot = animScene.GetJointAnimation(0);
	ITSCENE::FullDagPath rootDagPathMangled = jroot->TargetPathString();
	ITSCENE::FullDagPath rootDagPath		= animScene.GetReferenceNodeOriginalName(rootDagPathMangled);

	ITGEOM::Matrix4x4	bakedScaleMatrix;
	ITSCENE::FullDagPath bakedScaleNodeDagPath;
	if (skelScene.DetermineBakedScaleMatrix(rootDagPath, &bakedScaleMatrix, &bakedScaleNodeDagPath))
	{
		for (size_t animIndex = 0; animIndex < numJointAnims; ++animIndex)
		{
			const bool				   isRoot = (animIndex == 0);
			ITSCENE::JointAnimation*   janim = animScene.GetJointAnimation(animIndex);
			const ITSCENE::FullDagPath jointDagPathMangled = janim->TargetPathString();
			const ITSCENE::FullDagPath jointDagPath = animScene.GetReferenceNodeOriginalName(jointDagPathMangled);
			ITSCENE::Joint const*	  joint = animScene.GetJoint(jointDagPathMangled);
			if (joint && ITSCENE::IsChildPathOf(jointDagPath, bakedScaleNodeDagPath))
			{
				janim->BakeAnimScale(bakedScaleMatrix, isRoot);
			}
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static std::string GetAnimNbdInputNickName(const std::string& animName)
{
	return "animNbd-" + animName;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static std::string GetMotionDataNbdInputNickName(const std::string& animName)
{
	return "mmNbd-" + animName;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static BuildTransform_MotionMatching* CreateTransform(const BuildTransformContext* const pContext,
													  const libdb2::Actor* const pDbActor,
													  const libdb2::MotionMatchSetPtr& pSet,
													  const BuildTransform_MotionMatching::AnimToDataMap& actorAnimToNdb)
{
	if (!pSet)
	{
		return nullptr;
	}

	// Find the subset of anims in the actor that the set includes
	BuildTransform_MotionMatching::AnimToDataMap setMap;
	setMap.reserve(pSet->Anims().size());

	for (const libdb2::Anim* pAnim : pSet->Anims())
	{
		if (pAnim->m_flags.m_Disabled)
			continue;
		BuildTransform_MotionMatching::AnimToDataMap::const_iterator it = actorAnimToNdb.find(pAnim->Name());
		ASSERT(it != actorAnimToNdb.end());
		setMap.insert(*it);
	}
	ASSERT(setMap.size() > 0);

	if (!BuildTransform_MotionMatching::ValidateMotionMatchSet(*pSet, setMap))
	{
		return nullptr;
	}

	const std::string motionMatchingBo = pContext->m_toolParams.m_buildPathMotionMatchingBo + pDbActor->Name()
										 + FileIO::separator + pSet->Name() + ".bo";

	BuildTransform_MotionMatching *const pMotionMatching = new BuildTransform_MotionMatching(pContext, pSet);

	std::vector<TransformInput> dependencies;

	std::vector<BuildTransform*> exportXforms;
	for (const BuildTransform_MotionMatching::AnimToDataMap::value_type& setPair : setMap)
	{
		BuildTransform_MotionMatchingExport* const pExport = new BuildTransform_MotionMatchingExport(pSet,
																									 setPair.second);
		std::vector<TransformInput> exportDependencies;
		exportDependencies.emplace_back(TransformInput(setPair.second.m_animNdbFile.m_path, "AnimNdb"));
		exportDependencies.emplace_back(TransformInput(setPair.second.m_animData.m_skelNdbFilename, "SkelNdb"));
		exportDependencies.emplace_back(TransformInput(setPair.second.m_animData.m_rigInfoFilename, "RigNdb"));
		pExport->SetInputs(exportDependencies);

		const std::string ndbFileName = pContext->m_toolParams.m_buildPathMotionMatching + 
										pDbActor->Name() + 
										FileIO::separator + 
										pSet->Name() + 
										FileIO::separator + 
										setPair.first + 
										".ndb";

		TransformOutput exportOutput(ndbFileName, GetMotionDataNbdInputNickName(setPair.first));

		pExport->SetOutput(exportOutput);		
		pContext->m_buildScheduler.AddBuildTransform(pExport, pContext);

		TransformInput exportInput(ndbFileName, GetMotionDataNbdInputNickName(setPair.first));
		dependencies.push_back(exportInput);
	}
	
	dependencies.push_back(TransformInput(setMap.begin()->second.m_animData.m_rigInfoFilename, "RigNdb"));
	pMotionMatching->SetInputs(dependencies);

	pMotionMatching->SetOutput(TransformOutput(motionMatchingBo, "MotionMatchingBo"));

	return pMotionMatching;
}

/// --------------------------------------------------------------------------------------------------------------- ///
std::vector<std::string> BuildTransform_MotionMatching::CreateTransforms(const BuildTransformContext* const pContext,
																		 const libdb2::Actor* const pDbActor,
																		 const AnimToDataMap& animToNdbMap)
{
	std::vector<std::string> outputFiles;
	// Create one BuildTransform_MotionMatching per motion match set in the actor
	for (const libdb2::MotionMatchSetPtr& motionMatchSet : pDbActor->m_motionMatchSets)
	{
		if (BuildTransform_MotionMatching* const pXForm = CreateTransform(pContext,
																		  pDbActor,
																		  motionMatchSet,
																		  animToNdbMap))
		{
			pContext->m_buildScheduler.AddBuildTransform(pXForm, pContext);
			outputFiles.push_back(pXForm->GetOutputPath("MotionMatchingBo").AsAbsolutePath());
		}
	}
	return outputFiles;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_MotionMatching::BuildTransform_MotionMatching(const BuildTransformContext *const pContext, 
															const libdb2::MotionMatchSetPtr& pSet)
	: BuildTransform("MotionMatchingSet", pContext), m_pSet(pSet)
{
#if DEBUG_MOTION_MATCHING
	EnableForcedEvaluation();
#endif

	m_preEvaluateDependencies.SetString("mmVersion", MOTION_MATCHING_VERSION_STR);
	m_preEvaluateDependencies.SetString("setXML", m_pSet->Xml());
	m_preEvaluateDependencies.SetInt("distFunc", MM_HYBRID_DIST_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_MotionMatching::Evaluate()
{
	BigStreamWriter streamWriter(m_pContext->m_toolParams.m_streamConfig);

	const BuildPath boPath = GetOutputPath("MotionMatchingBo");

	IMsg::ChannelManager*	  channelManager = IMsg::ChannelManager::Instance();
	IMsgPrivate::ChannelRedis* channelRedis = static_cast<IMsgPrivate::ChannelRedis*>(channelManager->GetChannel(IMsg::kChannelRedis));

	// Build the motion match set
	{
		// Attach a log file
		std::string		logFilename		   = boPath.AsAbsolutePath() + ".log";
		IMsg::ChannelId stdoutLogChannelId = channelManager->AttachFile(IMsg::kVstdout, logFilename.c_str(), true);
		IMsg::ChannelId stderrLogChannelId = channelManager->AttachFile(IMsg::kVstderr, logFilename.c_str(), true);
		channelManager->SetVerbosityFilter(stdoutLogChannelId, IMsg::kDebug);
		channelManager->SetVerbosityFilter(stderrLogChannelId, IMsg::kDebug);
		IMsgPrivate::ChannelRedisChannelPusher mmChannel(channelRedis, "motion_match");
		WriteMotionMatchSet(*m_pSet, streamWriter);
		channelManager->DetachFile(IMsg::kVstdout, logFilename.c_str());
		channelManager->DetachFile(IMsg::kVstderr, logFilename.c_str());
	}

	NdiBoWriter boWriter(streamWriter);
	boWriter.Write();

	DataStore::WriteData(boPath, boWriter.GetMemoryStream());

	return BuildTransformStatus::kOutputsUpdated;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static AnimPointers GetSortedAnims(const libdb2::AnimList& list)
{
	AnimPointers sorted;
	sorted.reserve(list.size());

	for (const libdb2::Anim* pAnim : list)
	{
		sorted.push_back(pAnim);
	}

	std::sort(sorted.begin(), sorted.end(), [](const libdb2::Anim* a, const libdb2::Anim* b) 
	{
		StringId64 nameA = StringToStringId64(a->Name().c_str());
		StringId64 nameB = StringToStringId64(b->Name().c_str());
		return nameA < nameB;
	});

	return sorted;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static size_t GetNumPoseDimensions(const libdb2::MotionMatchSet& set)
{
	return set.Pose().Bodies().size() * 6 /* pos + vel per body*/ + 3 /* facing */;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static size_t GetNumGoalLocatorDimensions(const libdb2::MotionMatchSet& set)
{
	const size_t numGoalLocs = set.GetGoalLocs().size();
	return numGoalLocs * kNumGoalEntriesPerGoalLoc;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static size_t GetNumGoalDimensions(const libdb2::MotionMatchSet& set)
{
	const libdb2::MotionMatchGoals& mmGoals = set.Goals();

	const size_t numSamples = mmGoals.GetNumSamples();
	const size_t numPrevSamples = mmGoals.GetNumSamplesPrevTrajectory();

	const size_t numGoalLocRows = GetNumGoalLocatorDimensions(set);

	return ((numSamples + numPrevSamples) * kNumGoalEntriesPerSample) + numGoalLocRows;
}

/// --------------------------------------------------------------------------------------------------------------- ///
enum ExtraSampleDimensions
{
	kExtraDimensionBias,
	kExtraDimensionGroupId,
	kNumExtraDimensions
};

/// --------------------------------------------------------------------------------------------------------------- ///
static size_t GetNumExtraDimensions()
{
	return kNumExtraDimensions;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static size_t GetTotalNumDimensions(const libdb2::MotionMatchSet& set)
{
	const size_t pose = GetNumPoseDimensions(set);
	const size_t goal = GetNumGoalDimensions(set);
	const size_t extra = GetNumExtraDimensions();
	return pose + goal + extra;
} 

/// --------------------------------------------------------------------------------------------------------------- ///
struct LocomotionState
{
	Locator m_align;
	Vector m_velocity;
	Vector m_facingDir;
	Point m_pathPos;
	float m_yawSpeed;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class LocomotionChannels
{
public:
	static const ITSCENE::JointAnimation* FindChannel(const ITSCENE::SceneDb& animScene,
													  const std::string& exportAlias,
													  const std::string& exportNameSpace)
	{
		return pipeline3::AnimChannelEvaluator::FindAnimChannelAnimByExportAlias(animScene,
																				 exportAlias,
																				 exportNameSpace);
	}

	static LocomotionChannels GetLocomotionChannels(const libdb2::MotionMatchSet& set,
													const ITSCENE::SceneDb& animScene,
													pipeline3::AnimClampBehavior clamping,
													const AnimExportData& exportData,
													float stoppingFaceDist)
	{
		const ITSCENE::JointAnimation* pAlign	  = FindChannel(animScene, "align", exportData.m_exportNamespace);
		const ITSCENE::JointAnimation* pPath	  = FindChannel(animScene, "apReference-path", exportData.m_exportNamespace);
		const ITSCENE::JointAnimation* pFacingDir = FindChannel(animScene, "apReference-strafe-dir", exportData.m_exportNamespace);
		const ITSCENE::JointAnimation* pGoal	  = FindChannel(animScene, "apReference-goal", exportData.m_exportNamespace);

		IABORT_IF(!pAlign, "Anim scene has no align\n");

		return LocomotionChannels(set,
								  animScene,
								  pAlign,
								  pPath,
								  pFacingDir,
								  pGoal,
								  clamping,
								  stoppingFaceDist,
								  exportData);
	}

	LocomotionState Evaluate(const float t) const
	{
		LocomotionState ret;
		ret.m_align		= m_align.Evaluate(t);
		ret.m_velocity	= m_align.EvaluateVelocity(t);
		ret.m_facingDir = GetLocalZ(m_facingDir.Evaluate(t).GetRotation());
		ret.m_pathPos	= m_path.Evaluate(t).GetTranslation();
		ret.m_yawSpeed	= m_align.EvaluateYawSpeed(t);

		if (m_goalValid)
		{
			Locator goalLoc = m_goal.Evaluate(t);
			if (Dist(goalLoc.Pos(), SMath::kOrigin) < m_stoppingFaceDist)
			{
				ret.m_facingDir = GetLocalZ(goalLoc.Rot());
			}
		}

		IABORT_UNLESS(IsFinite(ret.m_align), "Bad anim align @ time %f\n", t);
		IABORT_UNLESS(IsFinite(ret.m_velocity), "Bad velocity @ time %f\n", t);
		IABORT_UNLESS(IsFinite(ret.m_facingDir), "Bad facing dir @ time %f\n", t);
		IABORT_UNLESS(IsFinite(ret.m_pathPos), "Bad path position @ time %f\n", t);
		IABORT_UNLESS(IsFinite(ret.m_yawSpeed), "Bad yaw speed @ time %f\n", t);

		return ret;
	}

	const pipeline3::AnimChannelEvaluator& Align() const { return m_align; }

	Locator GetGoalLocator(const std::string& locName, const int index) const
	{
		GoalLocMap::const_iterator it = m_goalLocs.find(locName);
		if (it == m_goalLocs.end())
		{
			return Locator(SMath::kIdentity);
		}

		const GoalLocEntry& entry = it->second;

		const Locator loc = entry.m_locChannel.GetSample(index);

		return loc;
	}

private:
	LocomotionChannels(const libdb2::MotionMatchSet& set,
					   const ITSCENE::SceneDb& animScene,
					   const ITSCENE::JointAnimation* pAlign,
					   const ITSCENE::JointAnimation* pPath,
					   const ITSCENE::JointAnimation* pFacingDir,
					   const ITSCENE::JointAnimation* pGoal,
					   pipeline3::AnimClampBehavior clamping,
					   float stoppingFaceDist,
					   const AnimExportData& exportData)
		: m_align(pAlign, animScene.GetSampledMayaFrames(), clamping)
		, m_path(pPath ? pPath : pAlign,
				 animScene.GetSampledMayaFrames(),
				 clamping,
				 pPath ? pAlign : nullptr)
		, m_facingDir(pFacingDir ? pFacingDir : pAlign,
					  animScene.GetSampledMayaFrames(),
					  clamping,
					  pFacingDir ? pAlign : nullptr)
		, m_goal(pGoal ? pGoal : pAlign,
				 animScene.GetSampledMayaFrames(),
				 clamping,
				 pGoal ? pAlign : nullptr)
		, m_goalValid(pGoal != nullptr)
	{
		const libdb2::MmGoalLocList& goalLocs = set.GetGoalLocs();

		for (const libdb2::MotionMatchGoalLocEntry& goalLoc : goalLocs)
		{
			const std::string& locName = goalLoc.GetLocName();

			const ITSCENE::JointAnimation* pLoc = FindChannel(animScene, locName, exportData.m_exportNamespace);

			if (!pLoc)
				continue;

			m_goalLocs.emplace(locName, GoalLocEntry(pLoc, animScene, clamping));
		}
	}

	pipeline3::AnimChannelEvaluator m_align;
	pipeline3::AnimChannelEvaluator m_path;
	pipeline3::AnimChannelEvaluator m_facingDir;
	pipeline3::AnimChannelEvaluator m_goal;

	class GoalLocEntry
	{
	public:
		GoalLocEntry(const ITSCENE::JointAnimation* pLoc,
					 const ITSCENE::SceneDb& animScene,
					 pipeline3::AnimClampBehavior clamping)
			: m_locChannel(pLoc, animScene.GetSampledMayaFrames(), clamping)
		{
		}

		pipeline3::AnimChannelEvaluator m_locChannel;
	};

	typedef std::map<std::string, GoalLocEntry> GoalLocMap;

	GoalLocMap m_goalLocs;

	float m_stoppingFaceDist;
	bool m_goalValid;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static AnimVector GetAnimAlignVectorForFrame(const LocomotionChannels& locomotion,
											 const int startSample,
											 const float endTime,
											 const int n)
{
	AnimVector result(n * kNumGoalEntriesPerSample);
	result.setZero();

	const pipeline3::AnimChannelEvaluator& alignChannel = locomotion.Align();
	const Locator startAlign = alignChannel.GetSample(startSample);
	const float startTime	 = alignChannel.GetSampleTime(startSample);

	const float dt = (n > 0) ? (endTime / n) : 0.0f;

	for (I32F i = 0; i < n; i++)
	{
		const float t = dt * (i + 1);

		const float sampleTime	= startTime + t;
		const float roundedTime = std::round(sampleTime * 1200.0) / 1200.0;

		LocomotionState curState = locomotion.Evaluate(roundedTime);

		const Point pos		= startAlign.UntransformPoint(curState.m_pathPos);
		const Vector vel	= startAlign.UntransformVector(curState.m_velocity);
		const Vector facing = startAlign.UntransformVector(curState.m_facingDir);

		I32F sampleVIndex = i * kNumGoalEntriesPerSample;

		result[sampleVIndex++] = pos.X();
		result[sampleVIndex++] = pos.Y();
		result[sampleVIndex++] = pos.Z();
		result[sampleVIndex++] = vel.X();
		result[sampleVIndex++] = vel.Y();
		result[sampleVIndex++] = vel.Z();
		result[sampleVIndex++] = facing.X();
		result[sampleVIndex++] = facing.Y();
		result[sampleVIndex++] = facing.Z();
		result[sampleVIndex++] = curState.m_yawSpeed;
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static AnimVector GetGoalLocsVectorForFrame(const LocomotionChannels& locomotion,
											const libdb2::MotionMatchSet& set,
											const int frame)
{
	const size_t numGoalLocDimensions = GetNumGoalLocatorDimensions(set);

	AnimVector result(numGoalLocDimensions);
	result.setZero();

	const libdb2::MmGoalLocList& goalLocs = set.GetGoalLocs();

	int i = 0;

	for (const libdb2::MotionMatchGoalLocEntry& goalLoc : goalLocs)
	{
		Locator loc = locomotion.GetGoalLocator(goalLoc.GetLocName(), frame);

		result[i + 0] = loc.Pos().X();
		result[i + 1] = loc.Pos().Y();
		result[i + 2] = loc.Pos().Z();

		i += 3;
	}

	IABORT_IF(i != numGoalLocDimensions,
			  "Somehow have incorrect number of goal locator rows (%d != %d)",
			  i,
			  numGoalLocDimensions);

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static AnimVector GetPoseVectorForFrame(const libdb2::MotionMatchSet& set,
										const pipeline3::AnimEvaluator& anim,
										const int frame)
{
	AnimVector result(GetNumPoseDimensions(set));
	result.setZero();

	int vIndex = 0;
	for (const libdb2::MotionMatchPoseBody& body : set.Pose().Bodies())
	{
		Locator jointLoc(SMath::kIdentity);
		Vector  vel(SMath::kZero);
		if (body.IsCenterOfMass())
		{
			jointLoc.SetTranslation(anim.ComputeCenterOfMassOs(frame));
			anim.ComputeCenterOfMassVelOs(frame, &vel);
		}
		else
		{
			anim.GetJointLocOs(body.JointName(), frame, &jointLoc);
			anim.GetJointVelOs(body.JointName(), frame, &vel);
		}

		result[vIndex++] = jointLoc.GetTranslation().X();
		result[vIndex++] = jointLoc.GetTranslation().Y();
		result[vIndex++] = jointLoc.GetTranslation().Z();

		result[vIndex++] = vel.X();
		result[vIndex++] = vel.Y();
		result[vIndex++] = vel.Z();
	}

	// Compute pose "facing" dir
	{
		Vector  facingDir(SMath::kZero);
		Locator facingJointLoc(SMath::kIdentity);
		if (anim.GetJointLocOs(set.Pose().Facing().JointName(), frame, &facingJointLoc))
		{
			Vector facingVectorOs = facingJointLoc.TransformVector(set.Pose().Facing().Axis());
			facingVectorOs.SetY(0.0f);
			facingDir = SafeNormalize(facingVectorOs, SMath::kZero);
		}
		result[vIndex++] = facingDir.X();
		result[vIndex++] = facingDir.Y();
		result[vIndex++] = facingDir.Z();
	}

	IABORT_UNLESS(vIndex == result.rows(), "Internal pose vector construction error (%d != %d)\n", vIndex, result.rows());

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdbRead(NdbStream& stream, const char* pSymName, AnimVector& v)
{
	NdbBeginArray array(stream, pSymName, "AnimVector");
	v = AnimVector(array.Length());

	for (int i = 0; !array.IsDone(); ++i)
	{
		float& val = v[i];
		NdbRead(stream, NULL, val);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdbWrite(NdbStream& stream, const char* pSymName, const AnimVector& v)
{
	NdbBeginArray array(stream, pSymName, "AnimVector", Ndb::kSingleType, v.rows());

	for (int r = 0; r < v.rows(); r++)
	{
		NdbWrite(stream, NULL, v[r]);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
class MotionMatchingAnimVectors
{
public:
	std::vector<int>& SampledFrames() { return m_sampledFrames; }
	const std::vector<int>& SampledFrames() const { return m_sampledFrames; }
	AnimVectorArray& Vectors() { return m_vectors; }
	const AnimVectorArray& Vectors() const { return m_vectors; }

	void NdbSerialize(NdbStream& stream, const char* pSymName = "mmanimvectors")
	{
		NdbBegin beginInstance(stream, pSymName, "MotionMatchingAnimVectors");

		BuildPipeline::NdbSerialize(stream, "m_layerName", m_layerName);
		BuildPipeline::NdbSerialize(stream, "m_sampledFrames", m_sampledFrames);
		BuildPipeline::NdbSerialize(stream, "m_vectors", m_vectors);
	}

	void SetLayerName(const std::string& name) { m_layerName = name; }
	const std::string& GetLayerName() const { return m_layerName; }

private:
	std::string m_layerName;
	std::vector<int> m_sampledFrames;
	AnimVectorArray m_vectors;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static bool LoadMotionMatchingAnimVectorsNdb(const BuildFile& ndbFile, MotionMatchingAnimVectors* data)
{
	NdbStream ndbStream;
	DataStore::ReadData(ndbFile, ndbStream);
	data->NdbSerialize(ndbStream);
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static MotionMatchingAnimVectors SampleAnim(const libdb2::MotionMatchSet& set,
											const MotionMatchingAnimData& mmData,
											const BuildFile& animSceneFile,
											const ITSCENE::SceneDb& skelScene,
											const BuildPipeline::RigInfo& rigInfo)
{
	INOTE_VERBOSE("Reading scene: %s\n", animSceneFile.AsAbsolutePath().c_str());

#ifdef DEBUG_MM_ANIM
	if (mmData.m_animData.m_animName == DEBUG_MM_ANIM)
	{
		INOTE("");
	}
#endif

	MotionMatchingAnimVectors sampleData;
	
	ITSCENE::SceneDb animScene;
	const bool success = LoadSceneNdb(animSceneFile, &animScene);

	const float fSampleRate = float(mmData.m_animData.m_sampleRate);

	IABORT_UNLESS(success, "Error processing %s", animSceneFile.AsAbsolutePath().c_str());

	const std::string layerName = set.GetAnimLayerName(mmData.m_animData.m_animName);
	sampleData.SetLayerName(layerName);

	INOTE_VERBOSE("Sampling Anim '%s' [layer: '%s']\n", mmData.m_animData.m_animName.c_str(), layerName.c_str());

	animScene.ConvertAnimEulerToQuat();
	BakeAnimScale(animScene, skelScene);

	const bool looping = mmData.m_animData.m_isLooping;
	const bool clampBegin = set.ShouldAnimTrajClampBegining(mmData.m_animData.m_animName);
	const bool clampEnd = set.ShouldAnimTrajClampEnding(mmData.m_animData.m_animName);

	const pipeline3::AnimClampBehavior animClamping = looping
														  ? pipeline3::AnimClampBehavior::kLooping
														  : (clampBegin && clampEnd)
																? pipeline3::AnimClampBehavior::kClampBoth
																: clampBegin
																	  ? pipeline3::AnimClampBehavior::kClampBeginning
																	  : clampEnd
																			? pipeline3::AnimClampBehavior::kClampEnding
																			: pipeline3::AnimClampBehavior::kClampNone;

	pipeline3::AnimEvaluator evaluator(animScene, rigInfo, animClamping, mmData.m_animExportData);

	const libdb2::MotionMatchGoals& mmGoals = set.Goals();
	const float lookAheadTime		= mmGoals.GetMaxSampleTime();
	const int   numLookAheadSamples = mmGoals.GetNumSamples();

	const float lookBehindTime		 = mmGoals.GetMaxSampleTimePrevTrajectory();
	const int   numLookBehindSamples = mmGoals.GetNumSamplesPrevTrajectory();

	IABORT_UNLESS(lookAheadTime >= 0.0f, "Invalid look ahead time %f\n", lookAheadTime);
	IABORT_UNLESS(numLookAheadSamples >= 0, "Invalid num look ahead samples %d\n", numLookAheadSamples);
	IABORT_UNLESS(lookBehindTime >= 0.0f, "Invalid look behind time %f\n", lookBehindTime);
	IABORT_UNLESS(numLookBehindSamples >= 0, "Invalid num look behind samples %d\n", numLookBehindSamples);

	const bool biasedAnim  = set.IsAnimBiased(mmData.m_animData.m_animName);
	const float sampleBias = biasedAnim ? 0.0f : 1.0f;
	const I32F groupId = set.GetAnimGroupId(mmData.m_animData.m_animName);

	const size_t samplesInFutureHorizon = ceilf(lookAheadTime * fSampleRate);
	const size_t samplesInPastHorizon = ceilf(lookBehindTime * fSampleRate);
	const bool evalLooping = evaluator.Looping();
	const size_t numTotalSamples = evaluator.NumSamples();

	const size_t minTailSamples = evalLooping ? 0 : ceilf((float)set.MinNumSamples() * fSampleRate / 30.0f);

	const size_t numLeadingSamples = (evalLooping || evaluator.ClampBeginning()) ? 0 : samplesInPastHorizon;
	const size_t numTrailingSamples = (evalLooping || evaluator.ClampEnding()) ? minTailSamples : Max(samplesInFutureHorizon, minTailSamples);

	IABORT_IF(numLeadingSamples + numTrailingSamples > numTotalSamples,
			  "Not enough frames to sample animation '%s'! Only have %d sample%s but need %d to create trajectory (%d for past and %d for future). Consider clamping beginning or end trajectories. [%s]\n",
			  mmData.m_animData.m_animName.c_str(),
			  numTotalSamples,
			  numTotalSamples > 1 ? "s" : "",
			  numLeadingSamples + numTrailingSamples,
			  numLeadingSamples,
			  numTrailingSamples,
			  mmData.m_animData.m_animFullName.c_str());

	const int minSample = 0 + numLeadingSamples;
	const int maxSample = numTotalSamples - numTrailingSamples;

	IABORT_IF(minSample >= maxSample, "Invalid sample range for '%s' (min %d, max %d)", mmData.m_animData.m_animName.c_str(), minSample, maxSample);

	INOTE_VERBOSE("Sampling '%s' from %d to %d (%d leading, %d trailing)\n",
				  mmData.m_animData.m_animName.c_str(),
				  minSample,
				  maxSample,
				  numLeadingSamples,
				  numTrailingSamples);

	LocomotionChannels locoChannels = LocomotionChannels::GetLocomotionChannels(set,
																				animScene,
																				animClamping,
																				mmData.m_animExportData,
																				mmGoals.StoppingFaceDist());

	const size_t numExtraDimensions = GetNumExtraDimensions();

	AnimVectorArray& vectorData = sampleData.Vectors();
	std::vector<int>& sampledFrames = sampleData.SampledFrames();

	for (I32F i = minSample; i < maxSample; ++i)
	{
		AnimVector p		  = GetPoseVectorForFrame(set, evaluator, i);
		AnimVector futureTraj = GetAnimAlignVectorForFrame(locoChannels, i, lookAheadTime, numLookAheadSamples);
		AnimVector pastTraj   = GetAnimAlignVectorForFrame(locoChannels, i, -lookBehindTime, numLookBehindSamples);
		AnimVector goalLocVec = GetGoalLocsVectorForFrame(locoChannels, set, i);

		const size_t numPoseRows = p.rows();
		const size_t numFutureTrajRows = futureTraj.rows();
		const size_t numPastTrajRows   = pastTraj.rows();
		const size_t numGoalLocsRows   = goalLocVec.rows();

		AnimVector v(numPoseRows + numFutureTrajRows + numPastTrajRows + numGoalLocsRows + numExtraDimensions);

		IABORT_IF(v.rows() != GetTotalNumDimensions(set),
				  "Incorrect amount of data to sample anim for motion matching (have: %d, need: %d)",
				  v.rows(),
				  GetTotalNumDimensions(set));

		// Concatenate the 3 vectors and the extras
		v.head(numPoseRows) = p;

		v.segment(numPoseRows, numFutureTrajRows) = futureTraj;

		v.segment(numPoseRows + numFutureTrajRows, numPastTrajRows) = pastTraj;

		v.segment(numPoseRows + numFutureTrajRows + numPastTrajRows, numGoalLocsRows) = goalLocVec;

		v[numPoseRows + numFutureTrajRows + numPastTrajRows + numGoalLocsRows + kExtraDimensionBias]	= sampleBias;
		v[numPoseRows + numFutureTrajRows + numPastTrajRows + numGoalLocsRows + kExtraDimensionGroupId] = float(groupId);

		vectorData.push_back(v);
		sampledFrames.push_back(i);
	}

	return sampleData;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void WriteAnimVector(BigStreamWriter& writer, const AnimVector& v)
{
	for (int i = 0; i < v.rows(); i++)
	{
		writer.WriteF(v[i]);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static BigStreamWriter::Item* WriteVectorTable(BigStreamWriter& writer, const AnimVectorArray& vectors)
{
	const int pageSize		 = 1047980;
	const int vectorSize	 = sizeof(float) * vectors[0].rows();
	const int vectorsPerPage = pageSize / vectorSize;
	const int numChunks		 = (vectors.size() + (vectorsPerPage - 1)) / vectorsPerPage;

	std::vector<BigStreamWriter::Item*> chunks;
	for (int iChunk = 0; iChunk < numChunks; iChunk++)
	{
		char name[1024];
		sprintf_s(name, "vector-table-chunk-%d", iChunk);

		BigStreamWriter::Item* pChunkItem = writer.StartItem(BigWriter::RAW_DATA, name);
		writer.Align(16);
		for (int iVec = iChunk * vectorsPerPage; iVec < (iChunk + 1) * vectorsPerPage && iVec < vectors.size(); iVec++)
		{
			WriteAnimVector(writer, vectors[iVec]);
		}
		writer.EndItem();
		chunks.push_back(pChunkItem);
	}

	BigStreamWriter::Item* pPointerTableItem = writer.StartItem(BigWriter::RAW_DATA, "vector-table-chunks");
	writer.AlignPointer();
	for (const BigStreamWriter::Item* pChunkItem : chunks)
	{
		writer.WritePointer(pChunkItem->GetItemLocation());
	}
	writer.EndItem();

	BigStreamWriter::StreamPos startPos = writer.GetCurrentStreamSize();
	BigStreamWriter::Item* pTableItem = writer.StartItem(BigWriter::RAW_DATA, "vector-table");
	writer.AlignPointer();

	BigStreamWriter::StreamPos endPos    = writer.GetCurrentStreamSize();
	Location tableLoc = writer.WritePointer(pPointerTableItem->GetItemLocation());

	IABORT_UNLESS(startPos == endPos, "Error writing motion matching vector table (%d != %d)\n", startPos, endPos);

	writer.Write4(vectorsPerPage);
	writer.Write4(vectors.size());
	writer.Write4(vectors.size() > 0 ? vectors[0].rows() : 0);
	writer.EndItem();

	return pTableItem;
}

/*
struct MMCAnimSample
{
	U16 m_animIndex;
	U16 m_sampleIndex;
};

struct MMAnimSampleRange
{
I32 m_startIndex;
I32 m_count;
};

struct MMAnimSampleTable
{
StringId64* m_aAnimIds;   //Sorted
MMCAnimSample* m_aSamples;//Sorted
MMAnimSampleRange* m_aAnimRanges;
I32 m_numAnims;
I32 m_numSamples;
};
*/

struct AnimRange
{
	int m_startIndex;
	int m_count;
};

/// --------------------------------------------------------------------------------------------------------------- ///
static BigStreamWriter::Item* WriteSampleTable(BigStreamWriter& writer,
											   const AnimPointers& sortedAnims,
											   const AnimIndicesMap& animToSamples)
{
	BigStreamWriter::Item* pAnimIdArray = writer.StartItem(BigWriter::RAW_DATA, "anim-names");
	writer.Align(8);
	for (const libdb2::Anim* pAnim : sortedAnims)
	{
		INOTE_DEBUG("Anim %s exporting %d samples\n", pAnim->Name().c_str(), animToSamples.find(pAnim)->second.size());
		const StringId64 animId = StringToStringId64(pAnim->Name().c_str(), true);
		writer.WriteStringId64(animId);
	}
	writer.EndItem();

	BigStreamWriter::Item* pAnimSampleArray = writer.StartItem(BigWriter::RAW_DATA, "anim-samples");
	writer.Align(4);
	int animIndex	   = 0;
	int samplesWritten = 0;
	std::vector<AnimRange> animRanges;

	for (const libdb2::Anim* pAnim : sortedAnims)
	{
		AnimIndicesMap::const_iterator it = animToSamples.find(pAnim);
		if (it != animToSamples.end())
		{
			const std::vector<int>& samples = it->second;

			AnimRange newRange = { samplesWritten, samples.size() };
			animRanges.push_back(newRange);

			for (int s : samples)
			{
				writer.Write2U(animIndex);
				writer.Write2U(s);
				samplesWritten++;
			}

			animIndex++;
		}
	}
	writer.EndItem();

	BigStreamWriter::Item* pAnimRanges = writer.StartItem(BigWriter::RAW_DATA, "anim-ranges");
	writer.Align(4);
	for (const AnimRange& range : animRanges)
	{
		writer.Write4(range.m_startIndex);
		writer.Write4(range.m_count);
	}
	writer.EndItem();

	BigStreamWriter::Item* pTable = writer.StartItem(BigWriter::RAW_DATA, "sample-table");
	writer.AlignPointer();
	writer.WritePointer(pAnimIdArray->GetItemLocation());
	writer.WritePointer(pAnimSampleArray->GetItemLocation());
	writer.WritePointer(pAnimRanges->GetItemLocation());
	writer.Write4(sortedAnims.size());
	writer.Write4(samplesWritten);
	writer.EndItem();

	return pTable;
}

/// --------------------------------------------------------------------------------------------------------------- ///
/*
	struct MMPoseBody
	{
		StringId64 m_jointId;
		bool m_isCenterOfMass;
		F32 m_positionWeight;
		F32 m_velocirtWeight;
	};

	struct MMPose
	{
		StringId64 m_facingJointId;
		MMPoseBody* m_aBodies;
		Vector m_facingAxisLs;
		F32 m_facingWegiht;
		F32 m_masterWeight;
		I32 m_numBodies;
	};

	struct MMGoalLocator
	{
		StringId64 m_locatorId;
		F32 m_goalLocWeight;
		F32 m_minGoalDist;
	};

	struct MMGoals
	{
		F32 m_maxTrajSampleTime;
		I32 m_numTrajSamples;

		F32 m_masterWeight;
		F32 m_positionWeight;

		F32 m_velocityWeight;
		F32 m_directionalWeight;
		F32 m_interimDirectionalWeight;
		F32 m_yawSpeedWeight;

		F32 m_animBiasWeight;
		F32 m_groupingWeight;

		F32 m_maxTrajSampleTimePrevTraj;
		I32 m_numTrajSamplesPrevTraj;

		F32 m_prevTrajWeight;
		F32 m_stoppingFaceDist;

		MMGoalLocator* m_aGoalLocators;
		U32 m_numGoalLocators;
	};

	struct MMSettings
	{
		MMPose m_pose;
		MMGoals m_goals;
		bool m_useNormalizedScales;
		bool m_useNormalizedData;
	};
*/
static BigStreamWriter::Item* WriteSettings(BigStreamWriter& writer, const libdb2::MotionMatchSet& set)
{
	BigStreamWriter::Item* pSettingsItem = writer.StartItem(BigWriter::RAW_DATA, "mm-settings");
	// Pose
	writer.Align(16);
	const BigStreamWriter::StreamPos poseStartPos = writer.Tell();
	const StringId64 facingJointId = StringToStringId64(set.Pose().Facing().JointName().c_str(), true);
	writer.WriteStringId64(facingJointId);
	writer.AlignPointer();
	Location bodyArrayLoc = writer.WriteNullPointer();
	
	writer.Align(16);
	writer.WriteVector(set.Pose().Facing().Axis());
	writer.WriteF(set.Pose().Facing().Weight());
	writer.WriteF(set.Pose().Weight());
	writer.Write4(set.Pose().Bodies().size());
	writer.Align(16);
	
	const libdb2::MmGoalLocList& goalLocs = set.GetGoalLocs();

	const size_t numGoalLocators = goalLocs.size();

	const size_t poseItemSize = writer.Tell(16) - poseStartPos;
	
	IABORT_UNLESS(poseItemSize % 16 == 0, "Misaligned pose item size %d\n", poseItemSize);
	IABORT_UNLESS(poseItemSize == 48, "Incorrect pose item size %d\n", poseItemSize);

	// Goals
	const libdb2::MotionMatchGoals& goals = set.Goals();
	writer.Align(8);
	const BigStreamWriter::StreamPos goalsStartPos = writer.Tell();

	// 0
	writer.WriteF(goals.GetMaxSampleTime());
	writer.Write4(goals.GetNumSamples());

	// 8
	writer.WriteF(goals.MasterWeight());
	writer.WriteF(goals.PositionWeight());

	// 16
	writer.WriteF(goals.VelocityWeight());
	writer.WriteF(goals.DirectionWeight());

	// 24
	writer.WriteF(goals.InterimDirectionWeight());
	writer.WriteF(goals.YawSpeedWeight());

	// 32
	writer.WriteF(goals.BiasWeight());
	writer.WriteF(goals.GroupingWeight());

	// 40
	writer.WriteF(goals.GetMaxSampleTimePrevTrajectory());
	writer.Write4(goals.GetNumSamplesPrevTrajectory());

	// 48
	writer.WriteF(goals.PrevTrajectoryWeight());
	writer.WriteF(goals.StoppingFaceDist());

	// 56
	Location goalLocArrayLoc = writer.WriteNullPointer();

	// 64
	writer.Write4U(numGoalLocators);
	writer.Write1U(0);
	writer.Write1U(0);
	writer.Write1U(0);
	writer.Write1U(0);

	// 72
	writer.Align(8);

	const size_t goalItemSize = writer.Tell(8) - goalsStartPos;

	IABORT_UNLESS(goalItemSize % 8 == 0, "Misaligned goal item size %d\n", goalItemSize);
	IABORT_UNLESS(goalItemSize == 72, "Incorrect goal item size %d\n", goalItemSize);

	writer.Write1U(set.UseNormalizedScales());
	writer.Write1U(set.UseNormalizedData());

	writer.Align(16);

	// Write the pose body array
	writer.Align(8);
	writer.SetPointer(bodyArrayLoc);
	for (const libdb2::MotionMatchPoseBody& body : set.Pose().Bodies())
	{
		writer.Align(8);
		const StringId64 bodyJointId = StringToStringId64(body.JointName().c_str(), true);
		writer.WriteStringId64(bodyJointId);
		writer.Write1U(body.IsCenterOfMass() ? 1 : 0);
		writer.Align(4);
		writer.WriteF(body.PositionWeight());
		writer.WriteF(body.VelocityWeight());
		writer.Align(8);
	}

	writer.Align(8);
	writer.SetPointer(goalLocArrayLoc);
	for (const libdb2::MotionMatchGoalLocEntry& goalLoc : goalLocs)
	{
		const std::string& name = goalLoc.GetLocName();

		const float locWeight = goalLoc.GetWeight();
		const float minDist	  = goalLoc.GetMinDist();

		writer.WriteStringId64(StringToStringId64(name.c_str(), true));
		writer.WriteF(locWeight);
		writer.WriteF(minDist);
	}

	writer.EndItem();

	return pSettingsItem;
}

/// --------------------------------------------------------------------------------------------------------------- ///
typedef Eigen::Matrix<float, Eigen::Dynamic, 1> VectorType;
using ErrorFunc = float (*)(const VectorType& v0, const VectorType& v1);

/// --------------------------------------------------------------------------------------------------------------- ///
static float StandardErrorFunc(const VectorType& v0, const VectorType& v1)
{
#if MM_HYBRID_DIST_FUNC
	const float distSquared = (v0 - v1).squaredNorm();
	const float distLinear	= Sqrt(distSquared);
	const float dist		= Min(distLinear, distSquared);
#else
	const float dist = (v0 - v1).norm();
#endif

	return dist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float VelocityErrorFunc(const VectorType& v0, const VectorType& v1)
{
	VectorType v0n = v0;
	VectorType v1n = v1;
	v0n.normalize();
	v1n.normalize();

	const float v0Len = v0.norm();
	const float v1Len = v1.norm();

	const float dotP = (v0Len > kSmallFloat && v1Len > kSmallFloat) ? v0n.dot(v1n) : 1.0f;

	IABORT_UNLESS((dotP <= 1.1f) && (IsFinite(dotP)), "Invalid dot product result %f\n", dotP);

	const float err = Abs(v0Len - v1Len) * (2.0f - dotP);
	return err;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float ComputeFeatureVariance(const AnimVectorArray& animVectorTable,
									const int nDim,
									const std::vector<int>& startIndices,
									ErrorFunc errFunc)
{
	AnimVector meanVec(nDim);
	meanVec.fill(0.0f);

	Eigen::VectorBlock<AnimVector> mean = meanVec.segment(0, nDim);
	int N = animVectorTable.size() * startIndices.size();

	for (const AnimVector& v : animVectorTable)
	{
		for (int startIndex : startIndices)
		{
			mean += v.segment(startIndex, nDim);
		}
	}
	mean /= N;

	float variance = 0.0f;
	for (const AnimVector& v : animVectorTable)
	{
		for (int startIndex : startIndices)
		{
			variance += errFunc(mean, v.segment(startIndex, nDim));
		}
	}

	variance /= N;
	return variance;
}

/// --------------------------------------------------------------------------------------------------------------- ///
template <typename V>
static float ComputeVariance(V vectors, const int numVectors, ErrorFunc errFunc)
{
	// Need to size the mean vector to the size of the result.
	VectorType mean = vectors(0);
	mean.fill(0.0f);

	for (int i = 0; i < numVectors; i++)
	{
		mean += vectors(i);
	}
	mean /= numVectors;

	float variance = 0.0f;
	for (int i = 0; i < numVectors; i++)
	{
		//variance += (mean - vectors(i)).squaredNorm();
		variance += errFunc(mean, vectors(i));
	}
	return variance / numVectors;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static AnimVector ComputeAutoWeights(const libdb2::MotionMatchSet& set, const AnimVectorArray& animVectorTable)
{
	const libdb2::MotionMatchGoals& mmGoals = set.Goals();

	struct FeatureBundle
	{
		std::string m_desc;
		int m_nDim;
		std::vector<int> m_startIndices;
		ErrorFunc m_errorFunc = StandardErrorFunc;
	};

	std::vector<FeatureBundle> bundles;

	// Make the bundles
	// Pose Features
	const size_t numPoseDimensions = GetNumPoseDimensions(set);
	const size_t numGoalDimensions = GetNumGoalDimensions(set);
	const size_t numExtraDimensions = GetNumExtraDimensions();

	int vIndex = 0;
	{
		for (const libdb2::MotionMatchPoseBody& body : set.Pose().Bodies())
		{
			FeatureBundle pos;
			pos.m_desc = body.GetDesc() + " Pos";
			pos.m_nDim = 3;
			pos.m_startIndices.push_back(vIndex);
			vIndex += 3;

			FeatureBundle vel;
			vel.m_desc = body.GetDesc() + " Vel";
			vel.m_nDim = 3;
			vel.m_startIndices.push_back(vIndex);
			vel.m_errorFunc = VelocityErrorFunc;
			vIndex += 3;

			bundles.push_back(pos);
			bundles.push_back(vel);
		}
		// Facing is not scaled because its a direction feature which is already unit
		vIndex += 3;
	}
	// Goal Features
	{
		FeatureBundle pos;
		pos.m_desc = "Goal Pos";
		pos.m_nDim = 3;

		FeatureBundle vel;
		vel.m_desc = "Goal Vel";
		vel.m_nDim = 3;
		vel.m_errorFunc = VelocityErrorFunc;

		for (int i = 0; i < mmGoals.GetNumSamples(); i++)
		{
			pos.m_startIndices.push_back(vIndex);
			vIndex += pos.m_nDim;
			vel.m_startIndices.push_back(vIndex);
			vIndex += vel.m_nDim;
			// Facing is not scaled
			vIndex += 3;
			// yaw speed is not scaled
			vIndex += 1;
		}

		bundles.push_back(pos);
		bundles.push_back(vel);
	}

	// Past Traj Features
	{
		FeatureBundle pos;
		pos.m_desc = "Past Traj Pos";
		pos.m_nDim = 3;

		FeatureBundle vel;
		vel.m_desc = "Past Traj Vel";
		vel.m_nDim = 3;
		vel.m_errorFunc = VelocityErrorFunc;

		for (int i = 0; i < mmGoals.GetNumSamplesPrevTrajectory(); i++)
		{
			pos.m_startIndices.push_back(vIndex);
			vIndex += pos.m_nDim;
			vel.m_startIndices.push_back(vIndex);
			vIndex += vel.m_nDim;
			// Facing is not scaled
			vIndex += 3;
			// yaw speed is not scaled
			vIndex += 1;
		}

		bundles.push_back(pos);
		bundles.push_back(vel);
	}

	for (U32F iExtra = 0; iExtra < kNumExtraDimensions; ++iExtra)
	{
		vIndex++; // Anim bias and other etxra dimensions are not scaled
	}

	AnimVector scales(animVectorTable[0].rows());
	AnimVector scaleFilter(animVectorTable[0].rows());
	scales.fill(1.0f);
	scaleFilter.fill(0.0f);

	for (const FeatureBundle& bundle : bundles)
	{
		float var = ComputeFeatureVariance(animVectorTable, bundle.m_nDim, bundle.m_startIndices, bundle.m_errorFunc);

		if (bundle.m_startIndices.size() > 0)
		{
			INOTE_VERBOSE("Bundle '%s' variance %d: %f\n", bundle.m_desc.c_str(), bundle.m_startIndices[0], var);
		}

		float stdDev = Sqrt(var);
		float scale  = (stdDev > 0.0f) ? (1.0 / stdDev) : 1.0f;

		for (int startIndex : bundle.m_startIndices)
		{
			scales.segment(startIndex, bundle.m_nDim).fill(scale);
			scaleFilter.segment(startIndex, bundle.m_nDim).fill(1.0f);
		}
	}

	auto poseVarianceLambda = [&](int i) {
		Eigen::Matrix<float, Eigen::Dynamic, 1> result;
		result = animVectorTable[i].cwiseProduct(scales).cwiseProduct(scaleFilter).segment(0, numPoseDimensions);
		return result;
	};

	auto goalVarianceLambda = [&](int i) {

		const AnimVector& base = animVectorTable[i];
		const AnimVector scaled = base.cwiseProduct(scales);
		const AnimVector scaledFiltered = scaled.cwiseProduct(scaleFilter);

		const Eigen::Matrix<float, Eigen::Dynamic, 1> result = scaledFiltered.segment(numPoseDimensions, numGoalDimensions);
		return result;
	};

	const float poseVariance = ComputeVariance(poseVarianceLambda, animVectorTable.size(), StandardErrorFunc);
	const float goalVariance = ComputeVariance(goalVarianceLambda, animVectorTable.size(), StandardErrorFunc);

	INOTE_VERBOSE("Pose variance %f\n", poseVariance);
	INOTE_VERBOSE("Goal variance %f\n", goalVariance);

	AnimVector one(animVectorTable[0].rows());
	one.fill(1.0f);

	AnimVector varianceScale(animVectorTable[0].rows());
	varianceScale.segment(0, numPoseDimensions).fill(1.0f / Sqrt(poseVariance));
	varianceScale.segment(numPoseDimensions, numGoalDimensions).fill(1.0f / Sqrt(goalVariance));
	//varianceScale.segment(numPoseDimensions + numGoalDimensions, numExtraDimensions).fill(1.0f);

	scales.cwiseProduct(((varianceScale - one).cwiseProduct(scaleFilter) + one));
	scales.tail(scales.rows() - (numPoseDimensions + numGoalDimensions + numExtraDimensions)).fill(0.0f);

	const float poseVariancePost = ComputeVariance(poseVarianceLambda, animVectorTable.size(), StandardErrorFunc);
	const float goalVariancePost = ComputeVariance(goalVarianceLambda, animVectorTable.size(), StandardErrorFunc);

	INOTE_VERBOSE("Post Pose variance %f\n", poseVariancePost);
	INOTE_VERBOSE("Post Goal variance %f\n", goalVariancePost);

	for (const FeatureBundle& bundle : bundles)
	{
		for (int startIndex : bundle.m_startIndices)
		{
			INOTE_VERBOSE("Auto weight for '%s' [index %d]:", bundle.m_desc.c_str(), startIndex);

			for (int i = 0; i < bundle.m_nDim; ++i)
			{
				INOTE_VERBOSE(" %f", scales[startIndex + i]);
			}

			INOTE_VERBOSE("\n");
		}
	}

	return scales;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static MetricVectors GetPoseScales(const libdb2::MotionMatchSet& set)
{
	AnimVector scales(GetNumPoseDimensions(set));
	scales.fill(1.0f);

	int index = 0;
	for (const libdb2::MotionMatchPoseBody& body : set.Pose().Bodies())
	{
		scales.segment(index, 3).fill(body.PositionWeight());
		index += 3;
		scales.segment(index, 3).fill(body.VelocityWeight());
		index += 3;
	}

	scales.segment(index, 3).fill(set.Pose().Facing().Weight());
	index += 3;

	AnimVector mins(scales.rows());
	mins.fill(0.0f);
	return { scales, mins };
}

/// --------------------------------------------------------------------------------------------------------------- ///
static MetricVectors GetGoalScales(const libdb2::MotionMatchSet& set)
{
	const size_t numDimensions = GetNumGoalDimensions(set);

	AnimVector scales(numDimensions);
	scales.fill(1.0f);

	AnimVector minimums(numDimensions);
	minimums.fill(0.0f);

	const libdb2::MotionMatchGoals& goals = set.Goals();

	const float posWeight = goals.PositionWeight();

	int index = 0;
	for (int i = 0; i < goals.GetNumSamples(); i++)
	{
		const bool tailSample = i == (goals.GetNumSamples() - 1);

		scales.segment(index, 3).fill(posWeight);
		index += 3;

		scales.segment(index, 3).fill(goals.VelocityWeight());
		index += 3;

		const float facingWeight = tailSample ? goals.DirectionWeight() : goals.InterimDirectionWeight();

		scales.segment(index, 3).fill(facingWeight);
		index += 3;

		scales[index] = goals.YawSpeedWeight();
		index += 1;
	}

	const float prevTrajWeight = goals.PrevTrajectoryWeight();

	for (int i = 0; i < goals.GetNumSamplesPrevTrajectory(); i++)
	{
		const bool tailSample = i == (goals.GetNumSamplesPrevTrajectory() - 1);

		scales.segment(index, 3).fill(posWeight * prevTrajWeight);
		index += 3;

		scales.segment(index, 3).fill(goals.VelocityWeight() * prevTrajWeight);
		index += 3;

		const float facingWeight = tailSample ? goals.DirectionWeight() : goals.InterimDirectionWeight();

		scales.segment(index, 3).fill(facingWeight * prevTrajWeight);
		index += 3;

		scales[index] = goals.YawSpeedWeight() * prevTrajWeight;
		index += 1;
	}

	const libdb2::MmGoalLocList& goalLocs = set.GetGoalLocs();
	for (const libdb2::MotionMatchGoalLocEntry goalLoc : goalLocs)
	{
		const float weight	= goalLoc.GetWeight();
		const float minDist = goalLoc.GetMinDist();

		scales.segment(index, 3).fill(weight);
		minimums.segment(index, 3).fill(minDist);
		index += 3;
	}

	index += kNumExtraDimensions;

	MetricVectors ret;
	ret.m_scale = scales;
	ret.m_minimum = minimums;

	return ret;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static AnimVector GetGoalFilterVector(const libdb2::MotionMatchSet& set)
{
	const libdb2::MotionMatchGoals& mmGoals = set.Goals();

	const size_t numDimensions = GetNumGoalDimensions(set);

	AnimVector scales(numDimensions);
	scales.fill(1.0f);
	int index = 0;
	for (int i = 0; i < mmGoals.GetNumSamples(); i++)
	{
		const bool tailSample = i == (mmGoals.GetNumSamples() - 1);

		// position
		scales.segment(index, 3).fill(1.0f);
		index += 3;

		// velocity
		scales.segment(index, 3).fill(1.0f);
		index += 3;

		// facing
		scales.segment(index, 3).fill(1.0f);
		index += 3;

		// yaw speed
		if (tailSample)
		{
			scales.segment(index, 1).fill(1.0f);
		}
		else
		{
			scales.segment(index, 1).fill(0.0f);
		}
		index += 1;
	}

	for (int i = 0; i < mmGoals.GetNumSamplesPrevTrajectory(); i++)
	{
		const bool tailSample = i == (mmGoals.GetNumSamplesPrevTrajectory() - 1);

		// position
		scales.segment(index, 3).fill(1.0f);
		index += 3;

		// velocity
		scales.segment(index, 3).fill(1.0f);
		index += 3;

		// facing
		scales.segment(index, 3).fill(1.0f);
		index += 3;

		// yaw speed
		if (tailSample)
		{
			scales.segment(index, 1).fill(1.0f);
		}
		else
		{
			scales.segment(index, 1).fill(0.0f);
		}
		index += 1;
	}

	const libdb2::MmGoalLocList& goalLocs = set.GetGoalLocs();

	for (const libdb2::MotionMatchGoalLocEntry& goalLoc : goalLocs)
	{
		scales.segment(index, 3).fill(1.0f); // goal loc pos
		index += 3;
	}

	return scales;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void LogIndex(const MotionMatchIndex& index)
{
	INOTE_VERBOSE("MM Index: %s\n", index.GetName().c_str());
	INOTE_VERBOSE("Num clusters: %d\n", index.Clusters().size());
	
	std::vector<int> clusterSizes(index.Clusters().size());
	std::transform(index.Clusters().begin(), index.Clusters().end(), clusterSizes.begin(),
		[](const MotionMatchIndex::ClusterExtents& extents) { return extents.m_end - extents.m_start; });

	std::sort(clusterSizes.begin(), clusterSizes.end());

	float mean = 0;
	for (int size : clusterSizes)
	{
		mean += size;
	}
	mean /= clusterSizes.size();

	float variance = 0;
	for (int size : clusterSizes)
	{
		variance += Sqr(mean - size);
	}
	variance /= clusterSizes.size();

	INOTE_VERBOSE("Cluster Size Mean: %f\n", mean);
	INOTE_VERBOSE("Cluster Std:       %f\n", Sqrt(variance));
	INOTE_VERBOSE("Cluster Sizes:\n");
	for (int size : clusterSizes)
	{
		INOTE_DEBUG("  %d\n", size);
	}

	INOTE_VERBOSE("Index Scales:\n");
	const AnimVector& scales = index.Metric().GetScaleVector();
	for (int i = 0; i < scales.rows(); ++i)
	{
		INOTE_VERBOSE(" scales[%d] = %f\n", i, scales[i]);
	}
	
	INOTE_VERBOSE("Index Minimums:\n");
	const AnimVector& mins = index.Metric().GetMinimumVector();
	for (int i = 0; i < mins.rows(); ++i)
	{
		INOTE_VERBOSE(" mins[%d] = %f\n", i, mins[i]);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
static std::vector<MotionMatchIndex> CreateIndices(const libdb2::MotionMatchSet& set,
												   const AnimVectorIndexMap& vectorsMap,
												   const AnimVectorArray& vectorTable,
												   const AnimVector& autoWeights)
{
	const libdb2::MotionMatchGoals& mmGoals = set.Goals();

	std::vector<MotionMatchIndex> indices;
	
	AnimVector scales = autoWeights;

	if (!set.UseNormalizedData())
	{
		scales.setOnes();
	}

	AnimVector mins(scales.rows());
	mins.setZero();

	MetricVectors poseMetrics = GetPoseScales(set);
	MetricVectors goalMetrics = GetGoalScales(set);
	const int poseRows = poseMetrics.m_scale.rows();
	const int goalRows = goalMetrics.m_scale.rows();

	const float poseMasterWeight = set.Pose().Weight();
	const float goalMasterWeight = mmGoals.MasterWeight();

	const AnimVector goalFilterVec = GetGoalFilterVector(set);
	const AnimVector goalScalesFiltered = goalMetrics.m_scale.cwiseProduct(goalFilterVec);

	float poseAutoWeight = 1.0f;
	float goalAutoWeight = 1.0f;

	if (set.UseNormalizedScales())
	{
		const float pose_variance_scale = poseMetrics.m_scale.squaredNorm() / float(poseRows);
		const float goal_variance_scale = goalScalesFiltered.squaredNorm() / float(goalRows);

		poseAutoWeight = 1.0f / Sqrt(pose_variance_scale);
		goalAutoWeight = 1.0f / Sqrt(goal_variance_scale);

		INOTE_DEBUG("pose scale variance: %f -> pose scale auto weight: %f\n", pose_variance_scale, poseAutoWeight);
		INOTE_DEBUG("goal scale variance: %f -> goal scale auto weight: %f\n", goal_variance_scale, goalAutoWeight);
	}

	const AnimVector compositePoseScale = poseMetrics.m_scale * poseAutoWeight * poseMasterWeight;
	scales.segment(0, poseRows)			= scales.segment(0, poseRows).cwiseProduct(compositePoseScale);

	const AnimVector compositeGoalScale = goalScalesFiltered * goalAutoWeight * goalMasterWeight;
	scales.segment(poseRows, goalRows)	= scales.segment(poseRows, goalRows).cwiseProduct(compositeGoalScale);

	scales[poseRows + goalRows + kExtraDimensionBias]	 = mmGoals.BiasWeight();
	scales[poseRows + goalRows + kExtraDimensionGroupId] = mmGoals.GroupingWeight();

	mins.segment(0, poseRows)		 = poseMetrics.m_minimum;
	mins.segment(poseRows, goalRows) = goalMetrics.m_minimum;

	INOTE_DEBUG("Metric Scales:\n");
	for (int i = 0; i < scales.rows(); i++)
	{
		INOTE_DEBUG("%d : %f\n", i, scales[i]);
	}
	INOTE_DEBUG("Metric Minimum:\n");
	for (int i = 0; i < mins.rows(); i++)
	{
		INOTE_DEBUG("%d : %f\n", i, mins[i]);
	}
	
	AnimVector zero(scales.rows());
	zero.setZero();

	for (const std::pair<std::string, AnimVectorIndexArray>& entry : vectorsMap)
	{
		const AnimVectorIndexArray& vectorIndices = entry.second;

		AnimVectorSubset vectorSubset(vectorIndices, vectorTable);

		//The metric function and the way its parameters are computed needs to match the runtime versions as well for the indices to be valid
		MotionMatchIndex index(entry.first, vectorSubset, scales, mins);

		index.Init(vectorSubset, 2.0f, zero);

		LogIndex(index);

		// sanity checking 
		{
			std::vector<std::tuple<float, int>> results;
			index.NearestNeighbors(vectorSubset, vectorSubset[0], results, 10);

			IABORT_UNLESS(std::get<0>(results[0]) == 0.0f,
						  "Internal indices creation error (results[0][0] is %f)\n",
						  std::get<0>(results[0]));
			IABORT_UNLESS(std::get<1>(results[0]) == 0,
						  "Internal indices creation error (results[0][1] is %d)\n",
						  std::get<1>(results[0]));
		}

		indices.push_back(index);
	}

	return indices;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static BigStreamWriter::Item* WriteIndices(BigStreamWriter& writer, const std::vector<MotionMatchIndex>& indices)
{
/*
	struct MMClusterExtents
	{
		I32 m_start;
		I32 m_end;
	};

	struct MMDistanceIndex
	{
		F32 m_dist;
		I32 m_index;
	};

	struct MMIndex
	{
		StringId64				m_nameId;
		MMAnimVectorTableRaw*	m_pMeansTable;
		MMClusterExtents*		m_aExtents;
		MMDistanceIndex*		m_aDistances;
		F32*					m_metricScaleVector;
		F32*					m_metricMinimunVector;
	};
*/

	struct IndexItems
	{
		std::string m_name;
		BigStreamWriter::Item* m_pMeans;
		BigStreamWriter::Item* m_pExtents;
		BigStreamWriter::Item* m_pDistances;
		BigStreamWriter::Item* m_pScaleVector;
		BigStreamWriter::Item* m_pMinimumVector;
	};

	std::vector<IndexItems> indexItems;

	U32F iIndex = 0;
	char nameBuf[128];

	for (const MotionMatchIndex& index : indices)
	{
		//Create the item components
		IndexItems items;
		items.m_name = index.GetName();
		items.m_pMeans = WriteVectorTable(writer, index.Means());

		sprintf_s(nameBuf, "index-extents-%d", iIndex);
		items.m_pExtents = writer.StartItem(BigWriter::RAW_DATA, nameBuf);
		
		for (const MotionMatchIndex::ClusterExtents& extent : index.Clusters())
		{			
			writer.Align(4);
			writer.Write4(extent.m_start);
			writer.Write4(extent.m_end);
			writer.Align(4);
		}		
		writer.EndItem();

		sprintf_s(nameBuf, "index-distances-%d", iIndex);
		items.m_pDistances = writer.StartItem(BigWriter::RAW_DATA, nameBuf);
		writer.Align(4);
		for (const MotionMatchIndex::DistanceIndex& dist : index.Distances())
		{
			const U32 iAnim = index.TranslateIndex(dist.m_index);
			writer.Align(4);
			writer.WriteF(dist.m_dist);
			writer.Write4(iAnim);
			writer.Align(4);
		}
		writer.EndItem();

		sprintf_s(nameBuf, "index-scales-%d", iIndex);
		items.m_pScaleVector = writer.StartItem(BigWriter::RAW_DATA, nameBuf);
		writer.Align(16);
		WriteAnimVector(writer, index.Metric().GetScaleVector());
		writer.EndItem();

		sprintf_s(nameBuf, "index-mins-%d", iIndex);
		items.m_pMinimumVector = writer.StartItem(BigWriter::RAW_DATA, nameBuf);
		writer.Align(16);
		WriteAnimVector(writer, index.Metric().GetMinimumVector());
		writer.EndItem();

		indexItems.push_back(items);

		++iIndex;
	}

	BigStreamWriter::Item* pIndexArrayItem = writer.StartItem(BigWriter::RAW_DATA, "kmnn-index");
	for (const IndexItems& items : indexItems)
	{
		const StringId64 nameId = StringToStringId64(items.m_name.c_str(), true);
		writer.AlignPointer();
		writer.WriteStringId64(nameId);
		writer.WritePointer(items.m_pMeans->GetItemLocation());
		writer.WritePointer(items.m_pExtents->GetItemLocation());
		writer.WritePointer(items.m_pDistances->GetItemLocation());
		writer.WritePointer(items.m_pScaleVector->GetItemLocation());
		writer.WritePointer(items.m_pMinimumVector->GetItemLocation());
		writer.AlignPointer();
	}
	writer.EndItem();

	return pIndexArrayItem;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static BigStreamWriter::Item* WriteLayers(BigStreamWriter& writer, const std::vector<std::string>& sortedLayerNames)
{
	BigStreamWriter::Item* pLayersItem = writer.StartItem(BigWriter::RAW_DATA, "layer-names");

	writer.Align(8);

	for (const std::string& layer : sortedLayerNames)
	{
		const StringId64 layerId = StringToStringId64(layer.c_str(), true);

		writer.WriteStringId64(layerId);
	}

	writer.EndItem();

	return pLayersItem;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static BigStreamWriter::Item* WriteLayerIndexTable(BigStreamWriter& writer, const std::vector<int>& indices)
{
	BigStreamWriter::Item* pIndicesItem = writer.StartItem(BigWriter::RAW_DATA, "layer-indices");

	for (int i : indices)
	{
		writer.Write1U(i);
	}

	writer.EndItem();

	return pIndicesItem;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform_MotionMatching::WriteMotionMatchSet(const libdb2::MotionMatchSet& set, BigStreamWriter& writer) const
{	
	// Sort the anims so the data is always in a consistent order
	AnimPointers sortedAnims = GetSortedAnims(set.Anims());
	
	// Load the skel ndb and rig infos here
	const BuildFile& rigNdbFile	   = GetInputFile("RigNdb");
	BuildPipeline::RigInfo rigInfo = LoadRigInfo(rigNdbFile);

	const size_t totalNumDimensions = GetTotalNumDimensions(set);

	AnimVectorArray allAnimVectors;
	AnimVectorIndexMap animVectorTable;
	AnimIndicesMap sampleIndices;

	std::vector<std::string> layerNamesTable;

	struct LayerData
	{
		U32 m_count = 0;
		I32 m_index = -1;
	};

	std::map<std::string, LayerData> layerDataMap;

	for (const libdb2::Anim* pAnim : sortedAnims)
	{
		if (pAnim->m_flags.m_Disabled)
			continue;

		const BuildFile& mmNdb = GetInputFile(GetMotionDataNbdInputNickName(pAnim->Name()));
		MotionMatchingAnimVectors data;
		if (!LoadMotionMatchingAnimVectorsNdb(mmNdb, &data))
		{
			IABORT("Error loading %s", mmNdb.AsAbsolutePath().c_str());
			return;
		}

		const std::string& layer = data.GetLayerName();
		const AnimVectorArray& dataVectors = data.Vectors();
		const size_t numVectors = dataVectors.size();

		for (U32F i = 0; i < numVectors; ++i)
		{
			layerNamesTable.push_back(layer);
		}

		AnimVectorIndexArray& animVectors = animVectorTable[layer];

		for (U32F i = 0; i < dataVectors.size(); ++i)
		{
			IABORT_UNLESS(dataVectors[i].rows() == totalNumDimensions,
						  "Animation '%s' data vector %i has mismatched dimensions (%d != %d)",
						  dataVectors[i].rows(),
						  totalNumDimensions);
			
			AnimVectorIndex e;
			e.m_iEntry = allAnimVectors.size() + i;
			animVectors.push_back(e);
		}

		// Add the vectors to the main table
		allAnimVectors.insert(allAnimVectors.end(), dataVectors.begin(), dataVectors.end());

		const std::vector<int>& sampledFrames = data.SampledFrames();
		sampleIndices.insert(std::make_pair(pAnim, std::move(sampledFrames)));

		LayerData& ldata = layerDataMap[layer];
		ldata.m_count++;
	}

	std::vector<std::string> sortedLayers;
	for (std::map<std::string, LayerData>::value_type& lpair : layerDataMap)
	{
		INOTE_VERBOSE("Exporting Layer: '%s' with %d anims\n", lpair.first.c_str(), lpair.second.m_count);

		sortedLayers.push_back(lpair.first);
		lpair.second.m_index = sortedLayers.size() - 1;
	}

	const size_t numLayers = sortedLayers.size();

	IABORT_IF(sortedLayers.size() > 256,
			  "Too many motion matching layers for set '%s' (%d)",
			  set.Name().c_str(),
			  sortedLayers.size());

	std::vector<int> layerIndicesTable;

	for (const std::string& layer : layerNamesTable)
	{
		layerIndicesTable.push_back(layerDataMap[layer].m_index);
	}

	const AnimVector autoWeights = ComputeAutoWeights(set, allAnimVectors);

	std::vector<MotionMatchIndex> indices = CreateIndices(set,
														  animVectorTable, 
														  allAnimVectors,
														  autoWeights);

	BigStreamWriter::Item* pTableItem		= WriteVectorTable(writer, allAnimVectors);
	BigStreamWriter::Item* pLayerIndexItem	= WriteLayerIndexTable(writer, layerIndicesTable);
	BigStreamWriter::Item* pSampleTableItem = WriteSampleTable(writer, sortedAnims, sampleIndices);
	BigStreamWriter::Item* pSettingsItem	= WriteSettings(writer, set);
	BigStreamWriter::Item* pIndicesItem		= WriteIndices(writer, indices);
	BigStreamWriter::Item* pLayersItem		= WriteLayers(writer, sortedLayers);

	/*
	struct MotionMatchingSetDef
	{
		StringId64				m_sidMotionMatch;	// Should be SID("MotionMatch")
		I32						m_version;			// Should be kMotionMatchVerion
		SkeletonId				m_skelId;
		U32						m_hierarchyId;
		I32						m_numDimensions;
		MMAnimVectorTableRaw*	m_pVectorTable;
		U8*						m_pLayerIndexTable;
		MMAnimSampleTable*		m_sampleTable;
		MMSettings*				m_pSettings;
		F32*					m_autoWeights;
		MMIndex*				m_aIndices;
		StringId64*				m_aLayerIds;
		I32						m_numIndices;
		U32						m_numLayers;
	};
	*/

	BigStreamWriter::Item* pScalesItem = writer.StartItem(BigWriter::RAW_DATA, "auto-weights");
	writer.Align(16);
	WriteAnimVector(writer, autoWeights);
	writer.EndItem();

	BigStreamWriter::Item* pItem = writer.StartItem(BigWriter::MOTION_MATCH_SET_1, set.Name());
	writer.WriteStringId64(StringToStringId64("MotionMatch"));
	writer.Write4(MOTION_MATCHING_VERSION);
	writer.WriteSkeletonId(rigInfo.GetSkelId());
	writer.Write4U(rigInfo.GetHierachyId());
	writer.Write4(autoWeights.rows());
	writer.AlignPointer();
	writer.WritePointer(pTableItem->GetItemLocation());
	writer.WritePointer(pLayerIndexItem->GetItemLocation());
	writer.WritePointer(pSampleTableItem->GetItemLocation());
	writer.WritePointer(pSettingsItem->GetItemLocation());
	writer.WritePointer(pScalesItem->GetItemLocation());
	writer.WritePointer(pIndicesItem->GetItemLocation());
	writer.WritePointer(pLayersItem->GetItemLocation());
	writer.Write4(indices.size());
	writer.Write4(numLayers);
	writer.EndItem();
	writer.AddLoginItem(pItem, BigWriter::MOTION_MATCHING);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool BuildTransform_MotionMatching::ValidateMotionMatchSet(const libdb2::MotionMatchSet& set,
														   const AnimToDataMap& animToNdbMap)
{
	bool valid = true;
	
	std::unordered_set<std::string> skelBoFileNames;
	std::unordered_set<std::string> ringInfoBoFileNames;

	for (const AnimToDataMap::value_type& p : animToNdbMap)
	{
		const MotionMatchingAnimData& data = p.second;
		skelBoFileNames.insert(data.m_animData.m_skelNdbFilename);
		ringInfoBoFileNames.insert(data.m_animData.m_rigInfoFilename);
	}
	if (skelBoFileNames.size() > 1)
	{
		IERR("Motion matching set referencing multiple skeletons: %s\n", set.Name().c_str());
		for (const std::string& name : skelBoFileNames)
		{
			IERR("%s\n", name.c_str());
		}
		valid = false;
	}
	else if (skelBoFileNames.size() == 0)
	{
		IERR("Motion matching references no skeleton: %s\n", set.Name().c_str());
		valid = false;
	}

	if (ringInfoBoFileNames.size() > 1)
	{
		IERR("Motion matching set referencing multiple rigs: %s\n", set.Name().c_str());
		for (const std::string& name : ringInfoBoFileNames)
		{
			IERR("%s\n", name.c_str());
		}
		valid = false;
	}

	for (const libdb2::NameString& animName : set.BiasedAnims())
	{
		if (animToNdbMap.find(animName.m_name) == animToNdbMap.end())
		{
			IERR("Bias animation '%s' not found in bundles for motion matching set '%s'\n",
				 animName.m_name.c_str(),
				 set.Name().c_str());
			valid = false;
		}
	}

	for (const libdb2::NameString& animName : set.TrajClampBeginAnims())
	{
		if (animToNdbMap.find(animName.m_name) == animToNdbMap.end())
		{
			IERR("Animation '%s' is in the trajectory clamp begin list but not found in bundles for motion matching set '%s'\n",
				 animName.m_name.c_str(),
				 set.Name().c_str());
			valid = false;
		}
	}

	for (const libdb2::NameString& animName : set.TrajClampEndAnims())
	{
		if (animToNdbMap.find(animName.m_name) == animToNdbMap.end())
		{
			IERR("Animation '%s' is in the trajectory clamp end list but not found in bundles for motion matching set '%s'\n",
				 animName.m_name.c_str(),
				 set.Name().c_str());
			valid = false;
		}
	}

	//IABORT_IF(!valid, "MotionMatching set '%s' has errors (see output)\n", set.Name().c_str());

	return valid;
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransform_MotionMatchingExport::BuildTransform_MotionMatchingExport(const libdb2::MotionMatchSetPtr& pSet,
																		 const MotionMatchingAnimData& animData)
	: BuildTransform("MotionMatchingExport")
	, m_pSet(pSet)
	, m_animData(animData)
{
#if DEBUG_MOTION_MATCHING
	EnableForcedEvaluation();
#endif

#ifdef DEBUG_MM_ANIM
	if (animData.m_animData.m_animName == DEBUG_MM_ANIM)
	{
		EnableForcedEvaluation();
	}
#endif

	PopulatePreEvalDependencies();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void BuildTransform_MotionMatchingExport::PopulatePreEvalDependencies()
{
	m_preEvaluateDependencies.SetConfigString("transformVersion", MOTION_MATCHING_VERSION_STR);

	m_preEvaluateDependencies.SetString("poseXML", m_pSet->Pose().Xml());
	m_preEvaluateDependencies.SetString("goalsXML", m_pSet->Goals().Xml());
	m_preEvaluateDependencies.SetString("goalLocsXML", m_pSet->GetGoalLocs().Xml());
	m_preEvaluateDependencies.SetBool("looping", m_animData.m_animData.m_isLooping);
	m_preEvaluateDependencies.SetBool("clampBegin", m_pSet->ShouldAnimTrajClampBegining(m_animData.m_animData.m_animName));
	m_preEvaluateDependencies.SetBool("clampEnd", m_pSet->ShouldAnimTrajClampEnding(m_animData.m_animData.m_animName));
	m_preEvaluateDependencies.SetBool("biased", m_pSet->IsAnimBiased(m_animData.m_animData.m_animName));
	m_preEvaluateDependencies.SetInt("groupId", m_pSet->GetAnimGroupId(m_animData.m_animData.m_animName));
	m_preEvaluateDependencies.SetInt("distFunc", MM_HYBRID_DIST_FUNC);
	m_preEvaluateDependencies.SetString("layerName", m_pSet->GetAnimLayerName(m_animData.m_animData.m_animName));
}

/// --------------------------------------------------------------------------------------------------------------- ///
BuildTransformStatus BuildTransform_MotionMatchingExport::Evaluate()
{
	const BuildPath& outputPath = GetFirstOutputPath();

	// Attach a log file
	IMsg::ChannelManager*	  channelManager = IMsg::ChannelManager::Instance();
	IMsgPrivate::ChannelRedis* channelRedis = static_cast<IMsgPrivate::ChannelRedis*>(channelManager->GetChannel(IMsg::kChannelRedis));
	std::string		logFilename = outputPath.AsAbsolutePath() + ".log";
	IMsg::ChannelId stdoutLogChannelId = channelManager->AttachFile(IMsg::kVstdout, logFilename.c_str(), true);
	IMsg::ChannelId stderrLogChannelId = channelManager->AttachFile(IMsg::kVstderr, logFilename.c_str(), true);
	channelManager->SetVerbosityFilter(stdoutLogChannelId, IMsg::kDebug);
	channelManager->SetVerbosityFilter(stderrLogChannelId, IMsg::kDebug);
	IMsgPrivate::ChannelRedisChannelPusher mmChannel(channelRedis, "motion_match_export");	

	// Load the skel ndb and rig infos here
	const BuildFile& skelNdbFile = GetInputFile("SkelNdb");
	const BuildFile& rigNdbFile	 = GetInputFile("RigNdb");
	ITSCENE::SceneDb skelScene;

	const bool successSkel = LoadSceneNdb(skelNdbFile, &skelScene);
	if (!successSkel)
	{
		IABORT("Error loading %s", skelNdbFile.AsAbsolutePath().c_str());
		return BuildTransformStatus::kFailed;
	}
	BuildPipeline::RigInfo rigInfo = LoadRigInfo(rigNdbFile);

	const BuildFile& animNdb = GetInputFile("AnimNdb");
	MotionMatchingAnimVectors data = SampleAnim(*m_pSet, m_animData, animNdb, skelScene, rigInfo);

	NdbStream stream;
	if (stream.OpenForWriting(Ndb::kBinaryStream) != Ndb::kNoError)
	{
		IABORT("Failed opening output file for motion match export data. [%s]\n", outputPath.AsPrefixedPath().c_str());
		return BuildTransformStatus::kFailed;
	}

	data.NdbSerialize(stream);
	stream.Close();
	
	DataStore::WriteData(outputPath, stream);

	channelManager->DetachFile(IMsg::kVstdout, logFilename.c_str());
	channelManager->DetachFile(IMsg::kVstderr, logFilename.c_str());

	return BuildTransformStatus::kOutputsUpdated;
}
