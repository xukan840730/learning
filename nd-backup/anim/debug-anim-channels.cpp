/*
 * Copyright (c) 2013 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/debug-anim-channels.h"

#include "corelib/containers/list-array.h"
#include "corelib/containers/statichash.h"
#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-debug.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/io/pak-structs.h"

#include "gamelib/gameplay/human-center-of-mass.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"

#define BUILD_WITH_JOBS true

#if ENABLE_DEBUG_ANIM_CHANNELS

const U32 kDebugAnimChannelCapacity = 6*1024;

STATIC_ASSERT(sizeof(AnimChannelFormat) == sizeof(float)*10);

DebugAnimChannels* DebugAnimChannels::s_pDebugAnimChannels = nullptr;

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugAnimChannels::Init()
{
	AllocateJanitor jj(kAllocDebug, FILE_LINE_FUNC);
	ANIM_ASSERT(s_pDebugAnimChannels == nullptr);
	s_pDebugAnimChannels = NDI_NEW DebugAnimChannels();
	s_pDebugAnimChannels->m_channelTable.Init(kDebugAnimChannelCapacity, FILE_LINE_FUNC);
}

/// --------------------------------------------------------------------------------------------------------------- ///
DebugAnimChannels* DebugAnimChannels::Get()
{	
	return s_pDebugAnimChannels;
}

/// --------------------------------------------------------------------------------------------------------------- ///
const CompressedChannel* DebugAnimChannels::GetChannel(const ArtItemAnim* pAnim, StringId64 channelNameId) const
{
	// you are intentionally allowed to search channels without a lock, it's too slow otherwise

	Key key(pAnim, channelNameId);
	ChannelTable::const_iterator it = m_channelTable.Find(key);	
	if (it != m_channelTable.End())
		return it->m_data;
	
	return nullptr;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugAnimChannels::AddChannel(const ArtItemAnim* pAnim, StringId64 channelNameId, const CompressedChannel* pChannel)
{
	ANIM_ASSERTF(m_lock.IsLocked(),
				 ("You must aquire a lock to use debug anim channel features (AtomicLockJanitor jj(DebugAnimChannels::Get()->GetLock(), FILE_LINE_FUNC))"));

	Key key(pAnim, channelNameId);
	
	ChannelTable::const_iterator it = m_channelTable.Find(key);	
	if (it == m_channelTable.End())
	{
		m_channelTable.Add(key, pChannel);		
	}
	else
	{
		ANIM_ASSERTF(false,
					 ("Attempting to add a channel %s that already exists in anim %s",
					  DevKitOnly_StringIdToString(channelNameId),
					  pAnim ? pAnim->GetName() : "<NULL>"));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugAnimChannels::RemoveChannelsForAnim(const ArtItemAnim* pAnim)
{
	ANIM_ASSERTF(m_lock.IsLocked(),
				 ("You must aquire a lock to use debug anim channel features (AtomicLockJanitor jj(DebugAnimChannels::Get()->GetLock(), FILE_LINE_FUNC))"));

	for (ChannelTable::Iterator it = m_channelTable.Begin(); it != m_channelTable.End();)
	{
		if (it->m_key.first == pAnim)
		{
			AnimChannelFormat* pChannelBuffer = reinterpret_cast<AnimChannelFormat*>(it->m_data->m_data);
			NDI_DELETE_ARRAY_CONTEXT(kAllocDebug, pChannelBuffer);
			it->m_data = nullptr;
			pChannelBuffer = nullptr;
			NDI_DELETE_CONTEXT(kAllocDebug, it->m_data);
			it->m_data = nullptr;
			m_channelTable.Erase(it, it);
		}
		else
		{
			++it;
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void DebugAnimChannels::GetChannelsForAnim(const ArtItemAnim* pAnim, ListArray<StringId64>& outChannelNames) const
{
	outChannelNames.Clear();
	for (ChannelTable::ConstIterator it = m_channelTable.Begin(); it != m_channelTable.End(); ++it)
	{
		if (it->m_key.first == pAnim)
		{
			outChannelNames.push_back(it->m_key.second);
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void DebugAnimChannels::PreAnimationLogout(const ArtItemAnim* pAnim)
{
	AtomicLockJanitor jj(&m_lock, FILE_LINE_FUNC);
	RemoveChannelsForAnim(pAnim);
}

struct sBuildChannelFrameParams
{
	IChannelSampleBuilder* pBuild;
	const ArtItemAnim* pAnim;
	int startFrame;
	int endFrame;
	AnimChannelFormat* pResult;
};

/// --------------------------------------------------------------------------------------------------------------- ///
JOB_ENTRY_POINT(BuildChannelFrame)
{
	sBuildChannelFrameParams* params = reinterpret_cast<sBuildChannelFrameParams*>(jobParam);
	for (int frame = params->startFrame; frame < params->endFrame; frame++)
	{
		params->pBuild->BuildFrame(params->pAnim, frame, &params->pResult[frame]);
	}
}


static NdAtomicLock s_buildLock;
/// --------------------------------------------------------------------------------------------------------------- ///
const CompressedChannel* DebugChannelBuilder::Build(const ArtItemAnim* pAnim, IChannelSampleBuilder* pBuild)
{
	AtomicLockJanitor lock(&s_buildLock, FILE_LINE_FUNC);

	int numFrames = pAnim->m_pClipData->m_numTotalFrames;

	//Allocate data for the channel out of debug
	CompressedChannel* pChannel = nullptr;
	AnimChannelFormat* pChannelBuffer = nullptr;

	if (Memory::IsDebugMemoryAvailable())
	{
		AllocateJanitor jj(kAllocDebug, FILE_LINE_FUNC);
		pChannel = NDI_NEW CompressedChannel;
		pChannel->m_numSamples = numFrames;
		pChannel->m_flags = kFlagCompression32BitFloats;
		pChannelBuffer = NDI_NEW AnimChannelFormat[numFrames];
		pChannel->m_data = reinterpret_cast<U8*>(pChannelBuffer);
	}	

	// Create our Job Array and Counter
	ndjob::JobArrayHandle jobArray;
	ndjob::CounterHandle pJobCounter;
	const int maxNumJobs = 6;
	if (BUILD_WITH_JOBS)
	{
		jobArray = ndjob::BeginJobArray(maxNumJobs);
		pJobCounter = ndjob::AllocateCounter(FILE_LINE_FUNC);
	}
	
	I64 numJobs = 0;

	const int framesPerJob = (numFrames + (maxNumJobs - 1)) / maxNumJobs;
	//For each frame in the anim
	{
		ScopedTempAllocator jobAlloc(FILE_LINE_FUNC);
		for (int iJob = 0; iJob < maxNumJobs; iJob++)
		{
			//	Kick a job to calculate the value at that frame and populate the channel
			sBuildChannelFrameParams* pJobArgs = NDI_NEW(kAlign16) sBuildChannelFrameParams;
			pJobArgs->pBuild = pBuild;
			pJobArgs->pAnim = pAnim;
			pJobArgs->startFrame = iJob*framesPerJob;
			pJobArgs->endFrame = Min(pJobArgs->startFrame + framesPerJob, numFrames);
			pJobArgs->pResult = pChannelBuffer;

			if (BUILD_WITH_JOBS)
			{
				// Set up the job decl and add the job to the array!
				ndjob::JobDecl jobDecl(BuildChannelFrame, (uintptr_t)pJobArgs);
				jobDecl.m_associatedCounter = pJobCounter;

				numJobs += ndjob::AddJobs(jobArray, &jobDecl, 1);
			}
			else if (pJobArgs->pResult)
			{
				BuildChannelFrame((uintptr_t)pJobArgs);
			}
		}
		if (BUILD_WITH_JOBS)
		{
			//Wait for all jobs
			pJobCounter->SetValue(numJobs);
			ndjob::CommitJobArray(jobArray);
			ndjob::WaitForCounterAndFree(pJobCounter);
		}
	}
	
	//Validate the results
	if (pChannel != nullptr)
	{
		for (int frame = 0; frame < numFrames; frame++)
		{
			ndanim::JointParams testParams;
			ReadFromCompressedChannel(pChannel, frame, &testParams);
			float transError = Dist(testParams.m_trans, Point(pChannelBuffer[frame].m_trans));
			float scaleError = Dist(AsPoint(testParams.m_scale), Point(pChannelBuffer[frame].m_scale));
			float quatDot = Dot(testParams.m_quat, pChannelBuffer[frame].m_quat);
			ANIM_ASSERT(transError < 0.001f);
			ANIM_ASSERT(scaleError < 0.001f);
			ANIM_ASSERT(quatDot >= 0.999f);
			ANIM_ASSERT(IsNormal(testParams.m_quat));
			ANIM_ASSERT(IsFinite(testParams.m_scale));
			ANIM_ASSERT(IsFinite(testParams.m_trans));
		}
	}

	return pChannel;
}

/// --------------------------------------------------------------------------------------------------------------- ///
JointChannelBuild::JointChannelBuild(I32 jointIndex) : m_jointIndex(jointIndex) {}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointChannelBuild::BuildFrame(const ArtItemAnim* pAnim, float sample, AnimChannelFormat* pResult) const
{
	ScopedTempAllocator jj(FILE_LINE_FUNC);

	const ArtItemSkeleton* pArtSkeleton = ResourceTable::LookupSkel(pAnim->m_skelID).ToArtItem();

	Transform* aJointTransforms = NDI_NEW Transform[pArtSkeleton->m_numGameplayJoints];
	ndanim::JointParams* aJointParams = NDI_NEW ndanim::JointParams[pArtSkeleton->m_numAnimatedGameplayJoints];

	bool valid = AnimateObject(Transform(kIdentity),
							   pArtSkeleton,
							   pAnim,
							   sample,
							   aJointTransforms,
							   aJointParams,
							   nullptr,
							   nullptr);

	ANIM_ASSERT(valid);
	if (valid)
	{
		Locator jointLocOS(aJointTransforms[m_jointIndex]);
		pResult->m_trans = jointLocOS.Pos();
		pResult->m_quat = jointLocOS.Rot();
		pResult->m_scale = aJointParams[m_jointIndex].m_scale;
	}	
}

/// --------------------------------------------------------------------------------------------------------------- ///
CenterOfMassChannelBuild::CenterOfMassChannelBuild() {}

/// --------------------------------------------------------------------------------------------------------------- ///
void CenterOfMassChannelBuild::BuildFrame(const ArtItemAnim* pAnim, float sample, AnimChannelFormat* pResult) const
{
	ScopedTempAllocator jj(FILE_LINE_FUNC);

	const ArtItemSkeleton* pArtSkeleton = ResourceTable::LookupSkel(pAnim->m_skelID).ToArtItem();

	Transform* aJointTransforms = NDI_NEW Transform[pArtSkeleton->m_numGameplayJoints];
	ndanim::JointParams* aJointParams = NDI_NEW ndanim::JointParams[pArtSkeleton->m_numAnimatedGameplayJoints];

	bool valid = AnimateObject(Transform(kIdentity),
							   pArtSkeleton,
							   pAnim,
							   sample,
							   aJointTransforms,
							   aJointParams,
							   nullptr,
							   nullptr);

	ANIM_ASSERT(valid);
	if (valid)
	{				
		pResult->m_trans = HumanCenterOfMass::ComputeCenterOfMassFromJoints(pAnim->m_skelID, aJointTransforms);
		pResult->m_quat = Quat(kIdentity);
		pResult->m_scale = Vec3(1.0f);
	}	
}

/// --------------------------------------------------------------------------------------------------------------- ///
RawDataChannelBuild::RawDataChannelBuild(float* pData, int numData) : m_pData(pData), m_numData(numData) {}

/// --------------------------------------------------------------------------------------------------------------- ///
void RawDataChannelBuild::BuildFrame(const ArtItemAnim* pAnim, float sample, AnimChannelFormat* pResult) const
{
	int index = Round(sample);
	ANIM_ASSERT(index >= 0 && index < m_numData);
	pResult->m_trans = Vec3(m_pData[index], 0.0f, 0.0f);
	pResult->m_quat = Quat(kIdentity);
	pResult->m_scale = Vec3(1.0f);
}

#endif
