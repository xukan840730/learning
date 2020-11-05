/*
 * Copyright (c) 2004 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/joint-sfx-helper.h"

#include "corelib/containers/statichash.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/bigsort.h"
#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"
#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-data.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-table.h"
#include "ndlib/anim/effect-anim-entry-tag.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/anim/joint-cache.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/io/file-system.h"
#include "ndlib/io/fileio.h"
#include "ndlib/io/pak-structs.h"
#include "ndlib/nd-config.h"
#include "ndlib/nd-game-info.h"


float JointSfxHelper::kHeelJointOffset = 0.02f;
float JointSfxHelper::kBallJointOffset = 0.03f;
float JointSfxHelper::kTopThreshold = 0.03f;
float JointSfxHelper::kBottomThreshold = 0.01f;
float JointSfxHelper::kTopSpeedThreshold = 0.03f;
float JointSfxHelper::kBottomSpeedThreshold = 0.01f;

const int JointSfxHelper::kMaxEffectAnims = 8192;
const int JointSfxHelper::kMaxGeneratedFootEffects = 8192;



/// --------------------------------------------------------------------------------------------------------------- ///
JointSfxHelper::JointSfxHelper(const FgAnimData* pAnimData, const char* destFolderPath) : m_pAnimData(pAnimData)
{
	AllocateJanitor jj(kAllocDebug, FILE_LINE_FUNC);

	m_pAllEffectAnims = NDI_NEW const EffectAnim* [kMaxEffectAnims];
	m_pExistingEffectAnims = NDI_NEW const EffectAnim* [kMaxEffectAnims];
	m_pNewEffectAnims = NDI_NEW EffectAnim [kMaxEffectAnims];
	m_pGeneratedFootEffects = NDI_NEW GeneratedFootEffect [kMaxGeneratedFootEffects];
	m_numTotalEffectAnims = 0;
	m_numExistingEffectAnims = 0;
	m_numNewEffectAnims = 0;
	m_numGeneratedFootEffects = 0;

	for (U32F i = 0; i < kNumBufferTypes; i++)
	{
		m_writeBuffers[i].m_pTextBuffer = NDI_NEW char [kTextBufferSize];
		m_writeBuffers[i].m_charsWritten = 0;
		m_writeBuffers[i].m_firstWrite = true;
	}

	strcpy(m_destFolder, destFolderPath);
}


/// --------------------------------------------------------------------------------------------------------------- ///
JointSfxHelper::~JointSfxHelper()
{
	for (U32F i = 0; i < kNumBufferTypes; i++)
	{
		delete [] m_writeBuffers[i].m_pTextBuffer;
	}

	delete [] m_pAllEffectAnims;
	delete [] m_pNewEffectAnims;
	delete [] m_pGeneratedFootEffects;
}


struct GatherEffAnimsData
{
	GatherEffAnimsData()
	{
		m_mode = 0;
		m_numNewEffectAnims = 0;
		m_numExistingEffectAnims = 0;
		m_numTotalEffectAnims = 0;
		m_maxEffectAnims = 0;
		m_numFound = 0;
		m_pNewEffectAnims = nullptr;
		m_pExistingEffectAnims = nullptr;
		m_pAllEffectAnims = nullptr;
	}

	U32 m_mode;
	U32 m_numNewEffectAnims;
	U32 m_numExistingEffectAnims;
	U32 m_numTotalEffectAnims;
	U32 m_maxEffectAnims;
	U32 m_numFound;
	EffectAnim* m_pNewEffectAnims;
	const EffectAnim** m_pExistingEffectAnims;
	const EffectAnim** m_pAllEffectAnims;
};


/// --------------------------------------------------------------------------------------------------------------- ///
static bool DoesEffectExistForAnimation(const EffectAnim** pEffectAnims, U32F numEffectAnims, StringId64 animId)
{
	for (U32F i = 0; i < numEffectAnims; ++i)
	{
		if (pEffectAnims[i]->m_nameId == animId)
			return true;
	}

	return false;
}


/// --------------------------------------------------------------------------------------------------------------- ///
static void GatherEffectAnimsFunctor(const ArtItemAnim* pArtItemAnim, uintptr_t data)
{
	GatherEffAnimsData* pData = (GatherEffAnimsData*)data;

	const EffectAnim* pEffectAnim = pArtItemAnim->m_pEffectAnim;
	if (!pEffectAnim)
		return;

	if (pData->m_mode == 0)
	{
		ALWAYS_ASSERT(pData->m_numTotalEffectAnims + 1 < pData->m_maxEffectAnims);
		ALWAYS_ASSERT(pData->m_numExistingEffectAnims + 1 < pData->m_maxEffectAnims);
		pData->m_pAllEffectAnims[pData->m_numTotalEffectAnims++] = pEffectAnim;
		pData->m_pExistingEffectAnims[pData->m_numExistingEffectAnims++] = pEffectAnim;
		++pData->m_numFound;
	}
	else
	{
		if (pArtItemAnim->m_flags & ArtItemAnim::kAdditive)
			return;

		// Ok we have an animation that plays on this skeleton...
		// Does it already exist in the pEffectAnims array?
		const StringId64 animId = StringToStringId64(pArtItemAnim->GetName());
		bool effectExist = DoesEffectExistForAnimation(pData->m_pAllEffectAnims, pData->m_numTotalEffectAnims, animId);
		if (!effectExist)
		{
			// Add a effect to the list
			ALWAYS_ASSERT(pData->m_numTotalEffectAnims + 1 < pData->m_maxEffectAnims);
			ALWAYS_ASSERT(pData->m_numNewEffectAnims + 1 < pData->m_maxEffectAnims);
			EffectAnim* pNewEffectAnim = &pData->m_pNewEffectAnims[pData->m_numNewEffectAnims++];
			pData->m_pAllEffectAnims[pData->m_numTotalEffectAnims++] = pNewEffectAnim;

			pNewEffectAnim->m_nameId = animId;
			pNewEffectAnim->m_numEffects = 0;
			pNewEffectAnim->m_pEffects = nullptr;
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
U32F JointSfxHelper::GatherExistingEffects(const SkeletonId skelId)
{
	if (!m_pAnimData || !m_pAnimData->m_curSkelHandle.ToArtItem())
		return 0;

	GatherEffAnimsData data;
	data.m_mode = 0;
	data.m_numExistingEffectAnims = m_numExistingEffectAnims;
	data.m_numTotalEffectAnims = m_numTotalEffectAnims;
	data.m_maxEffectAnims = kMaxEffectAnims;
	data.m_pExistingEffectAnims = m_pExistingEffectAnims;
	data.m_pAllEffectAnims = m_pAllEffectAnims;
	ResourceTable::ForEachAnimation(GatherEffectAnimsFunctor, m_pAnimData->m_curSkelHandle.ToArtItem()->m_skelId, (uintptr_t)&data);

	return data.m_numFound;
}


static I32 EffectAnimCompareFunc(const EffectAnim * a, const EffectAnim * b)
{
	ALWAYS_ASSERT(a && b);

	const char* aStr = DevKitOnly_StringIdToString(a->m_nameId);
	const char* bStr = DevKitOnly_StringIdToString(b->m_nameId);

	ALWAYS_ASSERT(aStr && bStr);
	return strcmp(aStr, bStr);
}

/// --------------------------------------------------------------------------------------------------------------- ///
extern bool IsSoundEffect(StringId64 effectId);
extern bool IsVoiceEffect(StringId64 effectId);

/// --------------------------------------------------------------------------------------------------------------- ///
bool JointSfxHelper::DidEffectExist(const EffectAnim* pEffectAnim) const
{
	for (U32F i = 0; i < m_numExistingEffectAnims; ++i)
	{
		if (m_pExistingEffectAnims[i] == pEffectAnim)
			return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSfxHelper::WriteEffects(bool includeExistingEffects, bool includeNewEffects, bool generateFootEffects)
{
	if (!Memory::IsDebugMemoryAvailable())
		return;
	const SkeletonId skelId = m_pAnimData->m_curSkelHandle.ToArtItem()->m_skelId;

	/************************************************************************/
	/* GATHER ALL EFFECT ANIMS                                              */
	/************************************************************************/
	{
		// Gather all existing effects (both sound and other)
		if (includeExistingEffects)
		{
			MsgOut("-----------------------------------------------------------------\n");
			MsgOut("Gathering existing effects...\n");

			GatherExistingEffects(skelId);

			MsgOut("Found %u effects.\n", (unsigned int)m_numExistingEffectAnims);
		}

		// Create new effect anims for all new animations
		if (includeNewEffects)
		{
			MsgOut("Gathering new animations without effects...\n");

			GatherEffAnimsData data;
			data.m_mode = 1;
			data.m_numNewEffectAnims = m_numNewEffectAnims;
			data.m_numTotalEffectAnims = m_numTotalEffectAnims;
			data.m_maxEffectAnims = kMaxEffectAnims;
			data.m_pNewEffectAnims = m_pNewEffectAnims;
			data.m_pAllEffectAnims = m_pAllEffectAnims;
			ResourceTable::ForEachAnimation(GatherEffectAnimsFunctor, m_pAnimData->m_curSkelHandle.ToArtItem()->m_skelId, (uintptr_t)&data);

// 			for (I32F index = 0; index < AnimMasterTable::m_numEntries; ++index)
// 			{
// 				AnimMasterTable::AnimTableSkelEntry& skelLod = AnimMasterTable::m_table[index];
// 				if (skelLod.m_skelId == skelId)
// 				{
// 					const ArtItemAnimGroup* pAnimGroup = skelLod.m_pAnimGroup;
// 					const U32 numAnims = pAnimGroup->GetAnimCount();
// 					for (U32 i = 0; i < numAnims; ++i)
// 					{
// 						const ArtItemAnim* pAnim = pAnimGroup->GetAnimByIndex(i);
// 						if (pAnim->m_skelID != skelId)
// 							continue;
// 
// 						if (pAnim->m_flags & ArtItemAnim::kAdditive)
// 							continue;
// 
// 						// Ok we have an animation that plays on this skeleton...
// 						// Does it already exist in the pEffectAnims array?
// 						const StringId64 animId = StringToStringId64(pAnim->GetName());
// 						bool effectExist = DoesEffectExistForAnimation(m_pAllEffectAnims, m_numTotalEffectAnims, animId);
// 						if (!effectExist)
// 						{
// 							// Add a effect to the list
// 							ALWAYS_ASSERT(m_numTotalEffectAnims + 1 < kMaxEffectAnims);
// 							ALWAYS_ASSERT(m_numNewEffectAnims + 1 < kMaxEffectAnims);
// 							EffectAnim* pNewEffectAnim = &m_pNewEffectAnims[m_numNewEffectAnims++];
// 							m_pAllEffectAnims[m_numTotalEffectAnims++] = pNewEffectAnim;
// 
// 							pNewEffectAnim->m_nameId = animId;
// 							pNewEffectAnim->m_numEffects = 0;
// 							pNewEffectAnim->m_pEffects = nullptr;
// 						}
// 					}
// 				}
//			}

			MsgOut("Found %u new animations.\n", (unsigned int)m_numNewEffectAnims);
		}
	}


	/************************************************************************/
	/* GENERATE FOOT EFFECTS												*/
	/************************************************************************/
	if (generateFootEffects)
	{
		MsgOut("Generating foot effects...\n");

		if (includeExistingEffects)
		{
			for (U32F i = 0; i < m_numExistingEffectAnims; ++i)
			{
				const EffectAnim* pEffectAnim = m_pExistingEffectAnims[i];
				GenerateFootEffectsForAnim(pEffectAnim);
			}
		}

		if (includeNewEffects)
		{
			for (U32F i = 0; i < m_numNewEffectAnims; ++i)
			{
				const EffectAnim* pEffectAnim = &m_pNewEffectAnims[i];
				GenerateFootEffectsForAnim(pEffectAnim);
			}
		}

		MsgOut("Generated %u foot effects.\n", (unsigned int)m_numGeneratedFootEffects);
	}


	/************************************************************************/
	/* SORT THE EFFECTS														*/
	/************************************************************************/
	{
		MsgOut("Sorting all effects.\n");

		QuickSort(m_pAllEffectAnims, m_numTotalEffectAnims, EffectAnimCompareFunc);

		MsgOut("Done sorting %u effects.\n", (unsigned int)m_numTotalEffectAnims);
	}

	/************************************************************************/
	/* OUTPUT THE FILES														*/
	/************************************************************************/
	{
		MsgOut("Writing EFF files.\n");

		for (U32F i = 0; i < m_numTotalEffectAnims; ++i)
		{
			const EffectAnim* pEffectAnim = m_pAllEffectAnims[i];
			const bool didExist = DidEffectExist(pEffectAnim);

			AddEffectAnimToBuffers(pEffectAnim, didExist);

			// If we have filled 80% of our buffer we write it out and clear it
			for (U32F type = 0; type < kNumBufferTypes; type++)
			{
				if (m_writeBuffers[type].m_charsWritten > kTextBufferSize * 0.8)
				{
					// MsgAnim("%s\n", m_writeBuffers[i].m_pTextBuffer);
					WriteBufferToEffectFile(m_pAnimData->m_curSkelHandle.ToArtItem()->GetName(), type);
					m_writeBuffers[type].m_charsWritten = 0;
					m_writeBuffers[type].m_firstWrite = false;
				}
			}
		}

		// If we have any text written we dump it out here
		for (U32F i = 0; i < kNumBufferTypes; i++)
		{
			if (m_writeBuffers[i].m_charsWritten)
			{
				// MsgAnim("%s\n", m_writeBuffers[i].m_pTextBuffer);
				WriteBufferToEffectFile(m_pAnimData->m_curSkelHandle.ToArtItem()->GetName(), i);
			}
		}

		MsgOut("Done writing EFF files.\n");
		MsgOut("-----------------------------------------------------------------\n");
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void JointSfxHelper::GenerateFootEffectsForAnim(const EffectAnim* pEffectAnim)
{
	const SkeletonId skelId = m_pAnimData->m_curSkelHandle.ToArtItem()->m_skelId;

	static ALIGNED(16) ndanim::ValidBits validBits[3];

	ScopedTempAllocator scopedAlloc(FILE_LINE_FUNC); // --- BEGIN SCOPED TEMP ------------------------------

	const U32F numTotalJoints = m_pAnimData->m_jointCache.GetNumTotalJoints();
	Transform* pJointTransforms = NDI_NEW (kAlign128) Transform[numTotalJoints];

	const U16 numInputControls = m_pAnimData->m_jointCache.GetNumInputControls();
	float* pInputControls = nullptr;
	if (numInputControls > 0)
	{
		pInputControls = NDI_NEW (kAlign16) float[numInputControls];
	}

	const U16 numOutputControls = m_pAnimData->m_jointCache.GetNumOutputControls();
	float* pOutputControls = nullptr;
	if (numOutputControls > 0)
	{
		pOutputControls = NDI_NEW (kAlign16) float[numOutputControls];
	}

	const int rightBallJointIndex = m_pAnimData->FindJoint(SID("r_ball"));
	const int rightHeelJointIndex = m_pAnimData->FindJoint(SID("r_heel"));
	const int leftBallJointIndex = m_pAnimData->FindJoint(SID("l_ball"));
	const int leftHeelJointIndex = m_pAnimData->FindJoint(SID("l_heel"));

	if (rightBallJointIndex < 0 ||
		rightHeelJointIndex < 0 ||
		leftBallJointIndex < 0 ||
		leftHeelJointIndex < 0)
		return;

	const ArtItemAnim* pAnim = AnimMasterTable::LookupAnim(skelId,
														   m_pAnimData->m_curSkelHandle.ToArtItem()->m_hierarchyId,
														   pEffectAnim->m_nameId)
								   .ToArtItem();

	if (pAnim == nullptr)
		return;

	// Remove all animations with no leg animations
	const int rightKneeJointIndex = m_pAnimData->FindJoint(SID("r_knee"));
	const int leftKneeJointIndex = m_pAnimData->FindJoint(SID("l_knee"));

	const ndanim::ValidBits* pValidBits = ndanim::GetValidBitsArray(pAnim->m_pClipData, 0);
	
	if (!pValidBits->IsBitSet(rightKneeJointIndex) && !pValidBits->IsBitSet(leftKneeJointIndex))
		return;


	m_leftHeelHist.Init(kTopThreshold + kHeelJointOffset, kBottomThreshold + kHeelJointOffset);
	m_leftBallHist.Init(kTopThreshold + kBallJointOffset, kBottomThreshold + kBallJointOffset);
	m_rightHeelHist.Init(kTopThreshold + kHeelJointOffset, kBottomThreshold + kHeelJointOffset);
	m_rightBallHist.Init(kTopThreshold + kBallJointOffset, kBottomThreshold + kBallJointOffset);
	m_leftHeelSpeedHist.Init(kTopSpeedThreshold, kBottomSpeedThreshold);
	m_leftBallSpeedHist.Init(kTopSpeedThreshold, kBottomSpeedThreshold);
	m_rightHeelSpeedHist.Init(kTopSpeedThreshold, kBottomSpeedThreshold);
	m_rightBallSpeedHist.Init(kTopSpeedThreshold, kBottomSpeedThreshold);
	m_lastLeftHeelPosOs = Point(SMath::kZero);
	m_lastLeftBallPosOs = Point(SMath::kZero);
	m_lastRightHeelPosOs = Point(SMath::kZero);
	m_lastRightBallPosOs = Point(SMath::kZero);
	m_lastLeftHeelPosWs = Point(SMath::kZero);
	m_lastLeftBallPosWs = Point(SMath::kZero);
	m_lastRightHeelPosWs = Point(SMath::kZero);
	m_lastRightBallPosWs = Point(SMath::kZero);


	// For each frame in the animation we evaluate the animation
	const I32F numTotalFrames = pAnim->m_pClipData->m_numTotalFrames;
	bool looping = ((pAnim->m_flags & ArtItemAnim::kLooping) != 0);

	for (I32F frameIndex = looping?-1:0; frameIndex < numTotalFrames; ++frameIndex)
	{
		I32F frameIndexReal = frameIndex;
		if (frameIndex == -1)
		{
			frameIndexReal = numTotalFrames - 1;
		}

		ndanim::JointParams params;
		SkeletonId skelId2 = NdGameObject::FromProcess(m_pAnimData->m_hProcess.ToProcess())->GetSkeletonId();
		EvaluateChannelParams evalParams;
		evalParams.m_pAnim		   = pAnim;
		evalParams.m_channelNameId = SID("align");
		evalParams.m_phase		   = static_cast<float>(frameIndexReal) * pAnim->m_pClipData->m_phasePerFrame;
		EvaluateChannelInAnim(skelId2, &evalParams, &params);

		// Animate the object using the specified animation and sample time.
		// Output the world-space joints in the passed in arrays.
		const Transform objectXform(params.m_quat, params.m_trans);
		AnimateObject(objectXform,
					  m_pAnimData->m_curSkelHandle.ToArtItem(),
					  pAnim,
					  static_cast<float>(frameIndexReal),
					  pJointTransforms,
					  nullptr,
					  pInputControls,
					  pOutputControls);

		// Extract out the existing effects for this frame range
		bool footEffectAlreadyExistOnFrame = false;
		if (frameIndexReal < (numTotalFrames - 1))
		{
			EffectList triggeredEffects;
			triggeredEffects.Init(10);
			EffectGroup::GetEffectsInSemiOpenInterval(pEffectAnim,
													  static_cast<float>(frameIndexReal),
													  static_cast<float>(frameIndexReal + 1),
													  numTotalFrames,
													  looping,
													  false,
													  true,
													  1.0f,
													  INVALID_STRING_ID_64,
													  0.0f,
													  true,
													  &triggeredEffects,
													  nullptr);

			for (U32F i = 0; i < triggeredEffects.GetNumEffects(); i++)
			{
				const EffectAnimInfo* pEffectAnimInfo = triggeredEffects.Get(i); 
				const EffectAnimEntryTag* pNameTag = pEffectAnimInfo->m_pEffect->GetTagByName(SID("name"));

				if (pNameTag)
				{
					// Don't generate foot effects on frames that already have a foot effect
					switch (pNameTag->GetValueAsStringId().GetValue())
					{
					case SID_VAL("lheel"):
					case SID_VAL("ltoe"):
					case SID_VAL("rheel"):
					case SID_VAL("rtoe"):
						footEffectAlreadyExistOnFrame = true;
						break;
					}
				}
			}
		}

		if (!footEffectAlreadyExistOnFrame)
		{
			// Collect the results
			GenerateProceduralFootEffects(pEffectAnim, frameIndexReal, objectXform, pJointTransforms, numTotalJoints, looping, frameIndex == -1);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSfxHelper::AddEffectAnimToBuffers(const EffectAnim* pEffectAnim, bool didExist)
{

	// See if we have any generated foot effects to add...
	const GeneratedFootEffect* pFootEffect = nullptr;
	for (U32F i = 0; i < m_numGeneratedFootEffects; ++i)
	{
		if (m_pGeneratedFootEffects[i].m_pEffectAnim == pEffectAnim)
		{
			pFootEffect = &m_pGeneratedFootEffects[i];
			break;
		}
	}

	// For empty effect animations we just add them to the existing or new sound eff file.
	if (pEffectAnim->m_numEffects == 0 && pFootEffect == nullptr)
	{
		AddAnimHeaderToBuffer(didExist ? kBufferSoundExisting : kBufferSoundNew, DevKitOnly_StringIdToString(pEffectAnim->m_nameId));
		AddAnimFooterToBuffer(didExist ? kBufferSoundExisting : kBufferSoundNew);
		return;
	}


	// Keep track of which buffers that need the footers
	bool effectAddedToBuffer[kNumBufferTypes];
	for (U32F i = 0; i < kNumBufferTypes; ++i)
	{
		effectAddedToBuffer[i] = false;
	}
	
	// Write out the decal buffers
	AddAnimHeaderToBuffer(kBufferDecal, DevKitOnly_StringIdToString(pEffectAnim->m_nameId));

	const GeneratedFootEffect* pFootDecalEffect = pFootEffect;
	while (pFootDecalEffect)
	{
		AddGeneratedFootDecalEffectToBuffer(kBufferDecal, pFootDecalEffect);
		pFootDecalEffect = pFootDecalEffect + 1;
		if (pFootDecalEffect->m_pEffectAnim != pEffectAnim)
			pFootDecalEffect = nullptr;
	}
	AddAnimFooterToBuffer(kBufferDecal);


	U32F effectIndex = 0;
	while(effectIndex < pEffectAnim->m_numEffects || pFootEffect)
	{
		const float effectFrame = effectIndex < pEffectAnim->m_numEffects ? pEffectAnim->m_pEffects[effectIndex].GetFrame() : kLargestFloat;
		const float footEffectFrame = pFootEffect ? static_cast<float>(pFootEffect->m_frame) : kLargestFloat;

		if (effectFrame == kLargestFloat && footEffectFrame == kLargestFloat)
			break;

		if (effectFrame < footEffectFrame)
		{
			const EffectAnimEntry* pEffectControl = &pEffectAnim->m_pEffects[effectIndex];
			StringId64 effectId = pEffectControl->GetNameId();
			if (IsSoundEffect(effectId) || IsVoiceEffect(effectId))
			{
				// Add to the 'Sound Existing or Misc New' file and add a header if needed
				U32F bufferType = didExist ? kBufferSoundExisting : kBufferSoundNew;
				if (!effectAddedToBuffer[bufferType])
				{
					AddAnimHeaderToBuffer(bufferType, DevKitOnly_StringIdToString(pEffectAnim->m_nameId));
					effectAddedToBuffer[bufferType] = true;
				}
				AddEffectToBuffer(bufferType, pEffectControl);

				// Add to the 'Sound All' file and add a header if needed
				bufferType = kBufferSoundAll;
				if (!effectAddedToBuffer[bufferType])
				{
					AddAnimHeaderToBuffer(bufferType, DevKitOnly_StringIdToString(pEffectAnim->m_nameId));
					effectAddedToBuffer[bufferType] = true;
				}
				AddEffectToBuffer(bufferType, pEffectControl);
			}
			else
			{
				// Add to the 'Misc Existing or Misc New' file and add a header if needed
				U32F bufferType = kBufferMiscExisting;
				if (!effectAddedToBuffer[bufferType])
				{
					AddAnimHeaderToBuffer(bufferType, DevKitOnly_StringIdToString(pEffectAnim->m_nameId));
					effectAddedToBuffer[bufferType] = true;
				}
				AddEffectToBuffer(bufferType, pEffectControl);

				// Add to the 'Misc All' file and add a header if needed
				bufferType = kBufferMiscAll;
				if (!effectAddedToBuffer[bufferType])
				{
					AddAnimHeaderToBuffer(bufferType, DevKitOnly_StringIdToString(pEffectAnim->m_nameId));
					effectAddedToBuffer[bufferType] = true;
				}
				AddEffectToBuffer(bufferType, pEffectControl);
			}
			++effectIndex;
		}
		else
		{
			// Add to the 'Sound Existing or Misc New' file and add a header if needed
			U32F bufferType = didExist ? kBufferSoundExisting : kBufferSoundNew;
			if (!effectAddedToBuffer[bufferType])
			{
				AddAnimHeaderToBuffer(bufferType, DevKitOnly_StringIdToString(pEffectAnim->m_nameId));
				effectAddedToBuffer[bufferType] = true;
			}
			AddGeneratedFootEffectToBuffer(bufferType, pFootEffect);

			// Add to the 'Sound All' file and add a header if needed
			bufferType = kBufferSoundAll;
			if (!effectAddedToBuffer[bufferType])
			{
				AddAnimHeaderToBuffer(bufferType, DevKitOnly_StringIdToString(pEffectAnim->m_nameId));
				effectAddedToBuffer[bufferType] = true;
			}
			AddGeneratedFootEffectToBuffer(bufferType, pFootEffect);

			pFootEffect = pFootEffect + 1;
			if (pFootEffect->m_pEffectAnim != pEffectAnim)
				pFootEffect = nullptr;
		}
	}


	for (U32F bufferType = 0; bufferType < kNumBufferTypes; ++bufferType)
	{
		if (effectAddedToBuffer[bufferType])
		{
			AddAnimFooterToBuffer(bufferType);
		}
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void JointSfxHelper::AddAnimHeaderToBuffer(U32F bufferType, const char* pAnimName)
{
	int charsWritten = m_writeBuffers[bufferType].m_charsWritten;
	char* pBuffer = m_writeBuffers[bufferType].m_pTextBuffer;

	charsWritten += sprintf(pBuffer + charsWritten, "anim \"%s\"\n", pAnimName);
	charsWritten += sprintf(pBuffer + charsWritten, "{\n");

	m_writeBuffers[bufferType].m_charsWritten = charsWritten;
}


/// --------------------------------------------------------------------------------------------------------------- ///
void JointSfxHelper::AddEffectToBuffer(U32F bufferType, const EffectAnimEntry* pCurrentEffect)
{
	int charsWritten = m_writeBuffers[bufferType].m_charsWritten;
	char* pBuffer = m_writeBuffers[bufferType].m_pTextBuffer;

	// frame "0.5"
	charsWritten += sprintf(pBuffer + charsWritten, "\t%s { frame \"%3.1f\"", DevKitOnly_StringIdToString(pCurrentEffect->GetNameId()), pCurrentEffect->GetFrame());

	for (U32F tagIndex = 0; tagIndex < pCurrentEffect->GetNumTags(); ++tagIndex)
	{
		const EffectAnimEntryTag* pTag = pCurrentEffect->GetTagByIndex(tagIndex);
		if (!pTag)
			continue;
		switch (pTag->GetNameId().GetValue())
		{
			/*
		case SID_VAL("frame"):	
			charsWritten += sprintf(pBuffer + charsWritten, " frame \"%3.1f\"", pTag->GetValueAsF32());
			break;
			*/
		case SID_VAL("name"):		
			charsWritten += sprintf(pBuffer + charsWritten, "\t\tname \"%s\"", DevKitOnly_StringIdToString(pTag->GetValueAsStringId()));
			break;
		case SID_VAL("joint"):	
			charsWritten += sprintf(pBuffer + charsWritten, "\t\tjoint \"%s\"", DevKitOnly_StringIdToString(pTag->GetValueAsStringId()));
			break;
		case SID_VAL("attach"):	
			charsWritten += sprintf(pBuffer + charsWritten, "\t\tattach \"%s\"", DevKitOnly_StringIdToString(pTag->GetValueAsStringId()));
			break;
		case SID_VAL("vol"):		
			charsWritten += sprintf(pBuffer + charsWritten, "\tvol \"%3.1f\"", pTag->GetValueAsF32());
			break;
		case SID_VAL("blendvol"):	
			charsWritten += sprintf(pBuffer + charsWritten, "\tblendvol \"%3.1f\"", pTag->GetValueAsF32());
			break;
		case SID_VAL("pitch"):	
			charsWritten += sprintf(pBuffer + charsWritten, "\tpitch \"%3.1f\"", pTag->GetValueAsF32());
			break;
		default:			
			charsWritten += sprintf(pBuffer + charsWritten, "\tUNHANDLED TAG %s", DevKitOnly_StringIdToString(pTag->GetNameId()));
			break;
		}
	}

	charsWritten += sprintf(pBuffer + charsWritten, " } \n");

	m_writeBuffers[bufferType].m_charsWritten = charsWritten;
}


/// --------------------------------------------------------------------------------------------------------------- ///
void JointSfxHelper::AddGeneratedFootEffectToBuffer(U32F bufferType, const GeneratedFootEffect* pGeneratedFootEffect)
{
	int charsWritten = m_writeBuffers[bufferType].m_charsWritten;
	char* pBuffer = m_writeBuffers[bufferType].m_pTextBuffer;

	if (pGeneratedFootEffect->m_type == SID("lheel"))
	{
		charsWritten += sprintf(pBuffer + charsWritten, "\tfoot-effect { frame \"%3.1f\"\t\tname \"lheel\"\t\tjoint \"l_heel\"\tvol \"-6.0\" } \n", (float)pGeneratedFootEffect->m_frame);
	}
	else if (pGeneratedFootEffect->m_type == SID("ltoe"))
	{
		charsWritten += sprintf(pBuffer + charsWritten, "\tfoot-effect { frame \"%3.1f\"\t\tname \"ltoe\" \t\tjoint \"l_ball\"\tvol \"-6.0\" } \n", (float)pGeneratedFootEffect->m_frame);
	}
	else if (pGeneratedFootEffect->m_type == SID("rheel"))
	{
		charsWritten += sprintf(pBuffer + charsWritten, "\tfoot-effect { frame \"%3.1f\"\t\tname \"rheel\"\t\tjoint \"r_heel\"\tvol \"-6.0\" } \n", (float)pGeneratedFootEffect->m_frame);
	}
	else if (pGeneratedFootEffect->m_type == SID("rtoe"))
	{
		charsWritten += sprintf(pBuffer + charsWritten, "\tfoot-effect { frame \"%3.1f\"\t\tname \"rtoe\" \t\tjoint \"r_ball\"\tvol \"-6.0\" } \n", (float)pGeneratedFootEffect->m_frame);
	}

	m_writeBuffers[bufferType].m_charsWritten = charsWritten;
}

void JointSfxHelper::AddGeneratedFootDecalEffectToBuffer(U32F bufferType, const GeneratedFootEffect* pGeneratedFootEffect)
{
	int charsWritten = m_writeBuffers[bufferType].m_charsWritten;
	char* pBuffer = m_writeBuffers[bufferType].m_pTextBuffer;

	//  min-blend \"0.5\"

	// Put in the decal effects
	if (pGeneratedFootEffect->m_type == SID("lheel"))
	{
		charsWritten += sprintf(pBuffer + charsWritten, "\tfoot-decal { frame \"%3.1f\"\t\tname \"lheel\"\t\tjoint \"l_heel\"\t} \n", (float)pGeneratedFootEffect->m_frame);
	}
	else if (pGeneratedFootEffect->m_type == SID("rheel"))
	{
		charsWritten += sprintf(pBuffer + charsWritten, "\tfoot-decal { frame \"%3.1f\"\t\tname \"rheel\"\t\tjoint \"r_heel\"\t} \n", (float)pGeneratedFootEffect->m_frame);
	}
	
	m_writeBuffers[bufferType].m_charsWritten = charsWritten;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSfxHelper::AddAnimFooterToBuffer(U32F bufferType)
{	
	int charsWritten = m_writeBuffers[bufferType].m_charsWritten;
	char* pBuffer = m_writeBuffers[bufferType].m_pTextBuffer;

	charsWritten += sprintf(pBuffer + charsWritten, "}\n\n");

	m_writeBuffers[bufferType].m_charsWritten = charsWritten;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSfxHelper::WriteBufferToEffectFile(const char* pArtGroupName, U32F bufferType)
{
	char pathName[256];
	sprintf( pathName, "%s/%s", EngineComponents::GetNdGameInfo()->m_pathDetails.m_localDir, m_destFolder );
	Err err = FileIO::makeDirPath(pathName);
	if (err.Failed())
		return;

	sprintf( pathName, "%s/artgroup/%s", pathName, pArtGroupName );
	err = FileIO::makeDirPath(pathName);
	if (err.Failed())
		return;

	char fullPathName[256];
	const char* pAppendName = GetAppendName(bufferType);
	sprintf( fullPathName, "%s/%s%s.eff", pathName, pArtGroupName, pAppendName );

	FileSystem* pFileSystem = EngineComponents::GetFileSystem();
	FileSystem::FileHandle fh;
	err = pFileSystem->OpenSync(fullPathName, &fh, m_writeBuffers[bufferType].m_firstWrite ? FS_O_CREAT | FS_O_WRONLY | FS_O_TRUNC : FS_O_CREAT | FS_O_WRONLY | FS_O_APPEND);
	if (err.Succeeded())
	{
		err = pFileSystem->WriteSync(fh, m_writeBuffers[bufferType].m_pTextBuffer, m_writeBuffers[bufferType].m_charsWritten);
	}

	err = pFileSystem->CloseSync(fh);
}

/// --------------------------------------------------------------------------------------------------------------- ///
const char* JointSfxHelper::GetAppendName(U32F bufferType)
{
	switch (bufferType)
	{
		case kBufferSoundAll:		return "-sound";
		case kBufferSoundNew:		return "-sound-new";
		case kBufferSoundExisting:	return "-sound-existing";
		case kBufferMiscAll:		return "-misc";
		case kBufferMiscNew:		return "-misc-new";		
		case kBufferMiscExisting:	return "-misc-existing";		
		case kBufferDecal:			return "-decal";		
	}

	return "";
}


/// --------------------------------------------------------------------------------------------------------------- ///
void JointSfxHelper::GenerateProceduralFootEffects(const EffectAnim* pEffectAnim, U32F frameIndex, const Transform& objectXform, const Transform* pJointTransforms, U32F numTotalJoints, bool looping, bool noWrite)
{
// 		MsgAnim("\nDetails for frame: %d\n", (int)frameIndex);

	const int leftHeelJointIndex = m_pAnimData->FindJoint(SID("l_heel"));
	const int leftBallJointIndex = m_pAnimData->FindJoint(SID("l_ball"));
	const int rightHeelJointIndex = m_pAnimData->FindJoint(SID("r_heel"));
	const int rightBallJointIndex = m_pAnimData->FindJoint(SID("r_ball"));

	const Transform& leftHeelXform = pJointTransforms[leftHeelJointIndex];
	const Transform& leftBallXform = pJointTransforms[leftBallJointIndex];
	const Transform& rightHeelXform = pJointTransforms[rightHeelJointIndex];
	const Transform& rightBallXform = pJointTransforms[rightBallJointIndex];

	const Point currentPosWs = objectXform.GetTranslation();
	const Vector localX = objectXform.GetXAxis();
	const Vector localY = objectXform.GetYAxis();
	const Vector localZ = objectXform.GetZAxis();

	const float kPlaneSize = 2.0f;
	const Point forward = currentPosWs + localZ * kPlaneSize;
	const Point backward = currentPosWs - localZ * kPlaneSize;
	const Point left = currentPosWs + localX * kPlaneSize;
	const Point right = currentPosWs - localX * kPlaneSize;

	const Point leftHeelPosWs = leftHeelXform.GetTranslation();
	const Point leftBallPosWs = leftBallXform.GetTranslation();
	const Point rightHeelPosWs = rightHeelXform.GetTranslation();
	const Point rightBallPosWs = rightBallXform.GetTranslation();

	const Transform inverseObjXform = Inverse(objectXform);

	const Point leftHeelPosOs = (leftHeelXform * inverseObjXform).GetTranslation();
	const Point leftBallPosOs = (leftBallXform * inverseObjXform).GetTranslation();
	const Point rightHeelPosOs = (rightHeelXform * inverseObjXform).GetTranslation();
	const Point rightBallPosOs = (rightBallXform * inverseObjXform).GetTranslation();

	if ((frameIndex == 0) && (!looping || noWrite))
	{
		m_lastLeftHeelPosOs = leftHeelPosOs;
		m_lastLeftBallPosOs = leftBallPosOs;
		m_lastRightHeelPosOs = rightHeelPosOs;
		m_lastRightBallPosOs = rightBallPosOs;

		m_lastLeftHeelPosWs = leftHeelPosWs;
		m_lastLeftBallPosWs = leftBallPosWs;
		m_lastRightHeelPosWs = rightHeelPosWs;
		m_lastRightBallPosWs = rightBallPosWs;
	}

	const Scalar leftHeelHeight = Dot(leftHeelPosOs - SMath::kOrigin, Vector(SMath::kUnitYAxis));
	const Scalar leftBallHeight = Dot(leftBallPosOs - SMath::kOrigin, Vector(SMath::kUnitYAxis));
	const Scalar rightHeelHeight = Dot(rightHeelPosOs - SMath::kOrigin, Vector(SMath::kUnitYAxis));
	const Scalar rightBallHeight = Dot(rightBallPosOs - SMath::kOrigin, Vector(SMath::kUnitYAxis));

	const bool wasLeftHeelDown = m_leftHeelHist.IsBelow();
	const bool wasLeftBallDown = m_leftBallHist.IsBelow();
	const bool wasRightHeelDown = m_rightHeelHist.IsBelow();
	const bool wasRightBallDown = m_rightBallHist.IsBelow();

	m_leftHeelHist.Update(leftHeelHeight);
	m_leftBallHist.Update(leftBallHeight);
	m_rightHeelHist.Update(rightHeelHeight);
	m_rightBallHist.Update(rightBallHeight);

	const bool currentLeftHeelDown = m_leftHeelHist.IsBelow();
	const bool currentLeftBallDown = m_leftBallHist.IsBelow();
	const bool currentRightHeelDown = m_rightHeelHist.IsBelow();
	const bool currentRightBallDown = m_rightBallHist.IsBelow();

// 		MsgAnim("Left Heel Height: %2.2f - %s\n", (float)leftHeelHeight, currentLeftHeelDown ? "down" : "up");
// 		MsgAnim("Left Ball Height: %2.2f - %s\n", (float)leftBallHeight, currentLeftBallDown ? "down" : "up");
// 		MsgAnim("Right Heel Height: %2.2f - %s\n", (float)rightHeelHeight, currentRightHeelDown ? "down" : "up");
// 		MsgAnim("Right Ball Height: %2.2f - %s\n", (float)rightBallHeight, currentRightBallDown ? "down" : "up");

	const Scalar leftHeelDeltaMoveOs = LengthSqr(m_lastLeftHeelPosOs - leftHeelPosOs);
	const Scalar leftBallDeltaMoveOs = LengthSqr(m_lastLeftBallPosOs - leftBallPosOs);
	const Scalar rightHeelDeltaMoveOs = LengthSqr(m_lastRightHeelPosOs - rightHeelPosOs);
	const Scalar rightBallDeltaMoveOs = LengthSqr(m_lastRightBallPosOs - rightBallPosOs);

	const Scalar leftHeelDeltaMoveWs = LengthSqr(m_lastLeftHeelPosWs - leftHeelPosWs);
	const Scalar leftBallDeltaMoveWs = LengthSqr(m_lastLeftBallPosWs - leftBallPosWs);
	const Scalar rightHeelDeltaMoveWs = LengthSqr(m_lastRightHeelPosWs - rightHeelPosWs);
	const Scalar rightBallDeltaMoveWs = LengthSqr(m_lastRightBallPosWs - rightBallPosWs);

	const bool wasLeftHeelStopped = m_leftHeelSpeedHist.IsBelow();
	const bool wasLeftBallStopped = m_leftBallSpeedHist.IsBelow();
	const bool wasRightHeelStopped = m_rightHeelSpeedHist.IsBelow();
	const bool wasRightBallStopped = m_rightBallSpeedHist.IsBelow();

	// Measure world space movement
	m_leftHeelSpeedHist.Update(leftHeelDeltaMoveWs * 100.0f);
	m_leftBallSpeedHist.Update(leftBallDeltaMoveWs * 100.0f);
	m_rightHeelSpeedHist.Update(rightHeelDeltaMoveWs * 100.0f);
	m_rightBallSpeedHist.Update(rightBallDeltaMoveWs * 100.0f);

	const bool leftHeelStopped = m_leftHeelSpeedHist.IsBelow();
	const bool leftBallStopped = m_leftBallSpeedHist.IsBelow();
	const bool rightHeelStopped = m_rightHeelSpeedHist.IsBelow();
	const bool rightBallStopped = m_rightBallSpeedHist.IsBelow();

// 		MsgAnim("Left Heel Delta Move: %2.2f - %s\n", (float)leftHeelDeltaMoveWs * 100.0f, leftHeelStopped ? "stopped" : "moving");
// 		MsgAnim("Left Ball Delta Move: %2.2f - %s\n", (float)leftBallDeltaMoveWs * 100.0f, leftBallStopped ? "stopped" : "moving");
// 		MsgAnim("Right Heel Delta Move: %2.2f - %s\n", (float)rightHeelDeltaMoveWs * 100.0f, rightHeelStopped ? "stopped" : "moving");
// 		MsgAnim("Right Ball Delta Move: %2.2f - %s\n", (float)rightBallDeltaMoveWs * 100.0f, rightBallStopped ? "stopped" : "moving");

	// Update object space locations
	m_lastLeftHeelPosOs = leftHeelPosOs;
	m_lastLeftBallPosOs = leftBallPosOs;
	m_lastRightHeelPosOs = rightHeelPosOs;
	m_lastRightBallPosOs = rightBallPosOs;

	// Update world space locations
	m_lastLeftHeelPosWs = leftHeelPosWs;
	m_lastLeftBallPosWs = leftBallPosWs;
	m_lastRightHeelPosWs = rightHeelPosWs;
	m_lastRightBallPosWs = rightBallPosWs;

	if (!noWrite)
	{
		if (currentLeftHeelDown && !wasLeftHeelStopped && leftHeelStopped)
		{
			ALWAYS_ASSERT(m_numGeneratedFootEffects + 1 < kMaxGeneratedFootEffects);
			GeneratedFootEffect* pNewFootEffect = &m_pGeneratedFootEffects[m_numGeneratedFootEffects++];
			pNewFootEffect->m_pEffectAnim = pEffectAnim;
			pNewFootEffect->m_type = SID("lheel");
			pNewFootEffect->m_frame = frameIndex;
		}

		if (currentLeftBallDown && !wasLeftBallStopped && leftBallStopped)
		{
			ALWAYS_ASSERT(m_numGeneratedFootEffects + 1 < kMaxGeneratedFootEffects);
			GeneratedFootEffect* pNewFootEffect = &m_pGeneratedFootEffects[m_numGeneratedFootEffects++];
			pNewFootEffect->m_pEffectAnim = pEffectAnim;
			pNewFootEffect->m_type = SID("ltoe");
			pNewFootEffect->m_frame = frameIndex;
		}

		if (currentRightHeelDown && !wasRightHeelStopped && rightHeelStopped)
		{
			ALWAYS_ASSERT(m_numGeneratedFootEffects + 1 < kMaxGeneratedFootEffects);
			GeneratedFootEffect* pNewFootEffect = &m_pGeneratedFootEffects[m_numGeneratedFootEffects++];
			pNewFootEffect->m_pEffectAnim = pEffectAnim;
			pNewFootEffect->m_type = SID("rheel");
			pNewFootEffect->m_frame = frameIndex;
		}

		if (currentRightBallDown && !wasRightBallStopped && rightBallStopped)
		{
			ALWAYS_ASSERT(m_numGeneratedFootEffects + 1 < kMaxGeneratedFootEffects);
			GeneratedFootEffect* pNewFootEffect = &m_pGeneratedFootEffects[m_numGeneratedFootEffects++];
			pNewFootEffect->m_pEffectAnim = pEffectAnim;
			pNewFootEffect->m_type = SID("rtoe");
			pNewFootEffect->m_frame = frameIndex;
		}
	}
}

