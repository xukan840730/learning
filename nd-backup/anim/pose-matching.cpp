/*
 * Copyright (c) 2011 Naughty Dog, Inc. 
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "ndlib/anim/pose-matching.h"

#include "corelib/math/mathutils.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/bigsort.h"

#include "ndlib/anim/anim-align-cache.h"
#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-options.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/script/script-manager.h"
#include "ndlib/scriptx/h/animation-script-types.h"
#include "ndlib/text/string-builder.h"

#include "gamelib/gameplay/ai/base/nd-ai-debug.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"
#include "gamelib/scriptx/h/pose-matching-defines.h"

/// --------------------------------------------------------------------------------------------------------------- ///
#if FINAL_BUILD
#define PMDebugPrint(...)
#else
#define PMDebugPrint(str, ...) if (debugPrint) { MsgOut(str, __VA_ARGS__); }
#endif

/// --------------------------------------------------------------------------------------------------------------- ///
static void ReadChannelForPoseMatch(const PoseMatchInfo& matchInfo,
									const ArtItemAnim* pAnim,
									const StringId64 channelId,
									const CompressedChannel* pChannel,
									U16 sampleIndex,
									ndanim::JointParams* pJpOut)
{
	ndanim::JointParams jointParams;

	const float phase = pAnim->m_pClipData->m_phasePerFrame * float(sampleIndex);
	Locator loc;
	const SkeletonId skelId = matchInfo.m_skelId ? matchInfo.m_skelId : pAnim->m_skelID;
	if (EvaluateChannelInAnimCached(skelId, pAnim, channelId, phase, false /* we manually mirror below */, &loc))
	{
		jointParams.m_quat = loc.Rot();
		jointParams.m_trans = loc.Pos();
		jointParams.m_scale = Vector(1.0f, 1.0f, 1.0f);
	}
	else
	{
		ReadFromCompressedChannel(pChannel, sampleIndex, &jointParams);
	}

	jointParams.m_trans	= (jointParams.m_trans - kOrigin) * matchInfo.m_strideTransform + Point(kOrigin);

	ANIM_ASSERT(IsFinite(jointParams.m_trans));
	ANIM_ASSERT(IsFinite(jointParams.m_quat));
	ANIM_ASSERT(IsFinite(jointParams.m_scale));

	if (matchInfo.m_mirror)
	{
		jointParams = MirrorJointParamsX(jointParams);
	}

	*pJpOut = jointParams;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct WorkingData
{
	StringId64 m_channelId = INVALID_STRING_ID_64;
	const CompressedChannel* m_pChannel = nullptr;
	Point m_matchPos = kOrigin;
	Vector m_matchVel = kZero;
	bool m_velValid = false;
};

/// --------------------------------------------------------------------------------------------------------------- ///
class JointSampleCache
{
public:
	JointSampleCache()
		: m_pCache(nullptr), m_numDataChannels(0), m_numSourceSamples(0), m_cacheRangeStart(0), m_cacheRangeCount(0)
	{
	}

	void Allocate(U32F numData, U32F numSourceSamples, U32F firstSample, U32F lastSample);
	void Build(const WorkingData* pData, const PoseMatchInfo& matchInfo, const ArtItemAnim* pAnim);

	ndanim::JointParams Read(U32F iData, U32F sample) const;
	Vector ReadVelocity(U32F iData, U32F sample) const;

	void DebugDraw(PrimServerWrapper& ps) const;

private:
	Vector BuildVelocityForSample(I32F iData, I32F curSample) const;

	I32F PreviousSample(I32F curSample) const
	{
		const bool looping = m_pAnim->m_flags & ArtItemAnim::kLooping;

		if (curSample <= 0)
		{
			if (looping)
			{
				return m_numSourceSamples - 2;
			}
			else
			{
				return m_numSourceSamples - 1;
			}
		}
		else
		{
			return curSample - 1;
		}
	}

	I32F NextSample(I32F curSample) const
	{
		const bool looping = m_pAnim->m_flags & ArtItemAnim::kLooping;

		if (looping && ((curSample + 1) == m_numSourceSamples))
		{
			return 1;
		}
		else
		{
			return (curSample + 1) % m_numSourceSamples;
		}
	}

	PoseMatchInfo m_matchInfo;
	const ArtItemAnim* m_pAnim;
	WorkingData* m_pData;
	ndanim::JointParams* m_pCache;
	Vector* m_pVelocityCache;

	U32 m_numDataChannels;
	U32 m_numSourceSamples;

	U32 m_cacheRangeStart;
	U32 m_cacheRangeCount;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSampleCache::Allocate(U32F numData, U32F numSourceSamples, U32F firstSample, U32F lastSample)
{
	if (numData <= 0)
		return;

	if (numSourceSamples <= 0)
		return;

	m_numDataChannels = numData;
	m_numSourceSamples = numSourceSamples;

	m_pData = NDI_NEW WorkingData[numData];
	
	if (firstSample > lastSample)
	{
		// wrapped
		m_cacheRangeCount = (m_numSourceSamples - firstSample) + lastSample + 1;
	}
	else
	{
		m_cacheRangeCount = (lastSample - firstSample) + 1;
	}

	m_cacheRangeStart = firstSample;

	m_pCache = NDI_NEW ndanim::JointParams[m_numDataChannels * m_cacheRangeCount];
	m_pVelocityCache = NDI_NEW Vector[m_numDataChannels * m_cacheRangeCount];
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSampleCache::Build(const WorkingData* pData, const PoseMatchInfo& matchInfo, const ArtItemAnim* pAnim)
{
	PROFILE(Animation, PM_BuildCache);

	m_matchInfo = matchInfo;
	m_pAnim = pAnim;

	for (U32F iData = 0; iData < m_numDataChannels; ++iData)
	{
		m_pData[iData] = pData[iData];

		for (U32F iS = 0; iS < m_cacheRangeCount; ++iS)
		{
			const U32F readIndex = (m_cacheRangeStart + iS) % m_numSourceSamples;

			ndanim::JointParams jp;

			ReadChannelForPoseMatch(matchInfo, pAnim, pData[iData].m_channelId, pData[iData].m_pChannel, readIndex, &jp);

			m_pCache[(iData * m_cacheRangeCount) + iS] = jp;
		}
	}

	for (U32F iData = 0; iData < m_numDataChannels; ++iData)
	{
		for (U32F iS = 0; iS < m_cacheRangeCount; ++iS)
		{
			const U32F readIndex = (m_cacheRangeStart + iS) % m_numSourceSamples;

			const Vector vel = BuildVelocityForSample(iData, readIndex);

			ANIM_ASSERT(IsFinite(vel));

			m_pVelocityCache[(iData * m_cacheRangeCount) + iS] = vel;
		}
	}

}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector JointSampleCache::BuildVelocityForSample(I32F iData, I32F curSample) const
{
	if (nullptr == m_pAnim)
	{
		return kZero;
	}

	const ndanim::ClipData* pClipData = m_pAnim->m_pClipData;
	const bool looping = m_pAnim->m_flags & ArtItemAnim::kLooping;
	const float secondsPerFrame = pClipData->m_secondsPerFrame;

	const I32F prevSample = PreviousSample(curSample);
	const I32F nextSample = NextSample(curSample);

	const ndanim::JointParams& jpPrev = Read(iData, prevSample);
	const ndanim::JointParams& jpCur = Read(iData, curSample);
	const ndanim::JointParams& jpNext = Read(iData, nextSample);

	Vector velocityB = kZero, velocityF = kZero;
	bool back = false, front = false;

	if ((prevSample < curSample) || looping)
	{
		// contribute back half
		velocityB = (jpCur.m_trans - jpPrev.m_trans) * secondsPerFrame;
		back = true;
	}

	if ((nextSample > curSample) || looping)
	{
		// contribute front half
		velocityF = (jpNext.m_trans - jpCur.m_trans) * secondsPerFrame;
		front = true;
	}

	ANIM_ASSERT(IsFinite(velocityB));
	ANIM_ASSERT(IsFinite(velocityF));

	if (front && back)
	{
		const Vector vel = Lerp(velocityB, velocityF, 0.5f);
		return vel;
	}
	else if (back)
	{
		return velocityB;
	}
	else if (front)
	{
		return velocityF;
	}

	return kZero;
}

/// --------------------------------------------------------------------------------------------------------------- ///
ndanim::JointParams JointSampleCache::Read(U32F iData, U32F sample) const
{
	ANIM_ASSERT(iData < m_numDataChannels);

	const U32F lastCacheSample = (m_cacheRangeStart + m_cacheRangeCount - 1) % m_numSourceSamples;

	ndanim::JointParams jp;

	if ((sample >= m_cacheRangeStart) && ((sample - m_cacheRangeStart) < m_cacheRangeCount))
	{
		const U32F mappedSampleIndex = sample - m_cacheRangeStart;

		const U32F readIndex = (iData * m_cacheRangeCount) + mappedSampleIndex;

		ANIM_ASSERT(readIndex < (m_numDataChannels * m_cacheRangeCount));

		jp = m_pCache[readIndex];

		ANIM_ASSERT(IsFinite(jp.m_trans));
		ANIM_ASSERT(IsFinite(jp.m_quat));
		ANIM_ASSERT(IsFinite(jp.m_scale));
	}
	else
	{
		ReadChannelForPoseMatch(m_matchInfo, m_pAnim, m_pData[iData].m_channelId, m_pData[iData].m_pChannel, sample, &jp);

		ANIM_ASSERT(IsFinite(jp.m_trans));
		ANIM_ASSERT(IsFinite(jp.m_quat));
		ANIM_ASSERT(IsFinite(jp.m_scale));
	}

	return jp;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector JointSampleCache::ReadVelocity(U32F iData, U32F sample) const
{
	ANIM_ASSERT(iData < m_numDataChannels);

	const U32F lastCacheSample = (m_cacheRangeStart + m_cacheRangeCount - 1) % m_numSourceSamples;

	Vector vel = kZero;

	if ((sample >= m_cacheRangeStart) && (sample <= lastCacheSample))
	{
		U32F mappedSampleIndex = 0;

		if (sample < m_cacheRangeStart)
		{
			mappedSampleIndex = (sample + m_numSourceSamples) - m_cacheRangeStart;
		}
		else
		{
			mappedSampleIndex = sample - m_cacheRangeStart;
		}

		const U32F readIndex = (iData * m_cacheRangeCount) + mappedSampleIndex;

		vel = m_pVelocityCache[readIndex];

		ANIM_ASSERT(IsFinite(vel));
	}
	else
	{
		vel = BuildVelocityForSample(iData, sample);
	}

	return vel;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void JointSampleCache::DebugDraw(PrimServerWrapper& ps) const
{
	STRIP_IN_FINAL_BUILD;

	ps.EnableHiddenLineAlpha();

	for (U32F iData = 0; iData < m_numDataChannels; ++iData)
	{
		const Color clr = AI::IndexToColor(iData, 0.33f);

		for (U32F i = 0; i < m_cacheRangeCount; ++i)
		{
			const ndanim::JointParams& jp = m_pCache[(iData * m_cacheRangeCount) + i];
			const ndanim::JointParams& jpN = m_pCache[(iData * m_cacheRangeCount) + ((i + 1) % m_cacheRangeCount)];

			const I32 sample = (m_cacheRangeStart + i) % m_numSourceSamples;

			ps.DrawString(jp.m_trans, StringBuilder<32>("%d", sample).c_str(), clr, 0.5f);
			ps.DrawCross(jp.m_trans, 0.01f, clr);
			ps.DrawLine(jp.m_trans, jpN.m_trans, kColorWhiteTrans, clr);
		}
	}

	ps.DisableHiddenLineAlpha();
}

/// --------------------------------------------------------------------------------------------------------------- ///
AnkleInfo::AnkleInfo()
{
	Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnkleInfo::Reset()
{
	m_anklePos[0] = m_anklePos[1] = kOrigin;
	m_ankleVel[0] = m_ankleVel[1] = kZero;
	m_ankleVelSpring[0].Reset();
	m_ankleVelSpring[1].Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnkleInfo::Update(Point_arg newLAnklePos,
					   Point_arg newRAnklePos,
					   const float dt,
					   Vector_arg groundNormalOs,
					   DC::AnkleInfo* pDCInfoOut)
{
	const Point prevLAnklePos = m_anklePos[0];
	const Point prevRAnklePos = m_anklePos[1];

	const Scalar invDeltaTime = (dt > 0.0f) ? (1.0f / dt) : 0.0f;
	const Vector lAnkleVel = (newLAnklePos - prevLAnklePos) * invDeltaTime;
	const Vector rAnkleVel = (newRAnklePos - prevRAnklePos) * invDeltaTime;

	m_anklePos[0] = newLAnklePos;
	m_anklePos[1] = newRAnklePos;

	const float springConst = g_animOptions.m_ankleVelMatchingSpringConst;
	m_ankleVel[0] = m_ankleVelSpring[0].Track(m_ankleVel[0], lAnkleVel, dt, springConst);
	m_ankleVel[1] = m_ankleVelSpring[1].Track(m_ankleVel[1], rAnkleVel, dt, springConst);

	if (pDCInfoOut)
	{
		pDCInfoOut->m_leftAnklePos = m_anklePos[0];
		pDCInfoOut->m_rightAnklePos = m_anklePos[1];
		pDCInfoOut->m_leftAnkleVel = m_ankleVel[0];
		pDCInfoOut->m_rightAnkleVel = m_ankleVel[1];
		pDCInfoOut->m_groundNormal = groundNormalOs;
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void AnkleInfo::DebugDraw(const Locator& alignWs) const
{
	STRIP_IN_FINAL_BUILD;

	const Point lAnklePosWs = alignWs.TransformPoint(m_anklePos[0]);
	const Point rAnklePosWs = alignWs.TransformPoint(m_anklePos[1]);

	g_prim.Draw(DebugArrow(lAnklePosWs, alignWs.TransformVector(m_ankleVel[0])));
	g_prim.Draw(DebugArrow(rAnklePosWs, alignWs.TransformVector(m_ankleVel[1])));

	g_prim.Draw(DebugString(lAnklePosWs, StringBuilder<128>("%0.2f", float(Length(m_ankleVel[0]))).c_str()));
	g_prim.Draw(DebugString(rAnklePosWs, StringBuilder<128>("%0.2f", float(Length(m_ankleVel[1]))).c_str()));
}

/// --------------------------------------------------------------------------------------------------------------- ///
WristInfo::WristInfo()
{
	Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WristInfo::Reset()
{
	m_wristPos[0] = m_wristPos[1] = kOrigin;
	m_wristVel[0] = m_wristVel[1] = kZero;
	m_wristVelSpring[0].Reset();
	m_wristVelSpring[1].Reset();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WristInfo::Update(Point_arg newLWristPos, Point_arg newRWristPos, const float dt, DC::WristInfo* pDCInfoOut)
{
	const Point prevLWristPos = m_wristPos[0];
	const Point prevRWristPos = m_wristPos[1];

	const Scalar invDeltaTime = (dt > 0.0f) ? (1.0f / dt) : 0.0f;
	const Vector lWristVel = (newLWristPos - prevLWristPos) * invDeltaTime;
	const Vector rWristVel = (newRWristPos - prevRWristPos) * invDeltaTime;

	m_wristPos[0] = newLWristPos;
	m_wristPos[1] = newRWristPos;

	const float springConst = g_animOptions.m_ankleVelMatchingSpringConst;
	m_wristVel[0] = m_wristVelSpring[0].Track(m_wristVel[0], lWristVel, dt, springConst);
	m_wristVel[1] = m_wristVelSpring[1].Track(m_wristVel[1], rWristVel, dt, springConst);

	if (pDCInfoOut)
	{
		pDCInfoOut->m_leftWristPos = m_wristPos[0];
		pDCInfoOut->m_rightWristPos = m_wristPos[1];
		pDCInfoOut->m_leftWristVel = m_wristVel[0];
		pDCInfoOut->m_rightWristVel = m_wristVel[1];
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void WristInfo::DebugDraw(const Locator& alignWs) const
{
	STRIP_IN_FINAL_BUILD;

	const Point lAnklePosWs = alignWs.TransformPoint(m_wristPos[0]);
	const Point rAnklePosWs = alignWs.TransformPoint(m_wristPos[1]);

	g_prim.Draw(DebugArrow(lAnklePosWs, alignWs.TransformVector(m_wristVel[0])));
	g_prim.Draw(DebugArrow(rAnklePosWs, alignWs.TransformVector(m_wristVel[1])));

	g_prim.Draw(DebugString(lAnklePosWs, StringBuilder<128>("%0.2f", float(Length(m_wristVel[0]))).c_str()));
	g_prim.Draw(DebugString(rAnklePosWs, StringBuilder<128>("%0.2f", float(Length(m_wristVel[1]))).c_str()));
}

/// --------------------------------------------------------------------------------------------------------------- ///
static float LoopingPhaseLerp(float fromPhase, float toPhase, float alpha)
{
	if (fromPhase <= toPhase)
	{
		return Lerp(fromPhase, toPhase, alpha);
	}

	const float deltaEnd = 1.0f - fromPhase;
	const float deltaBegin = toPhase;
	const float delta = (deltaEnd + deltaBegin) * alpha;
	const float interpPhase = fmodf(fromPhase + delta, 1.0f);
	return interpPhase;
}

/// --------------------------------------------------------------------------------------------------------------- ///
PoseMatchResult CalculateBestPoseMatchPhase(const ArtItemAnim* pAnim,
											const PoseMatchInfo& matchInfo,
											PoseMatchAlternates* pAlternatesOut /*= nullptr*/)
{
	PROFILE(Animation, CalculateBestPoseMatchPhase);
	PROFILE_ACCUM(CalculateBestPoseMatchPhase);

	if (!pAnim)
		return PoseMatchResult(-1.0f);
	
	for (U32F iEntry = 0; iEntry < PoseMatchInfo::kMaxEntries; ++iEntry)
	{
		if (!matchInfo.m_entries[iEntry].m_valid)
			break;
		
		//g_animAlignCache.TryCacheAnim(pAnim, matchInfo.m_entries[iEntry].m_channelId, matchInfo.m_startPhase, matchInfo.m_endPhase);
		g_animAlignCache.TryCacheAnim(pAnim, matchInfo.m_entries[iEntry].m_channelId, 0.0f, 1.0f);
	}

#if !FINAL_BUILD
	PrimServerWrapper ps(matchInfo.m_debugDrawLoc);
	ps.SetDuration(matchInfo.m_debugDrawTime);
#endif

	const bool debug = matchInfo.m_debug;
	const bool debugPrint = matchInfo.m_debugPrint;

	const ArtItemAnim* pAnimLs = pAnim;
	if (!pAnimLs)
		return PoseMatchResult(-1.0f);

	const ndanim::ClipData* pClipDataLs = pAnimLs->m_pClipData;
	const float secondsPerFrame = pClipDataLs->m_secondsPerFrame;

	PMDebugPrint("\nCalculateBestPoseMatch %s %d %f %s\n", pAnimLs->GetName(), pClipDataLs->m_numTotalFrames, pClipDataLs->m_phasePerFrame, matchInfo.m_mirror ? "mirror" : "");

	const Quat groundRotAdjust = QuatFromVectors(matchInfo.m_inputGroundNormalOs, kUnitYAxis);

	WorkingData data[PoseMatchInfo::kMaxEntries];
	U32 numData = 0;
	I32 numSourceSamples = -1;
	for (U32F iEntry = 0; iEntry < PoseMatchInfo::kMaxEntries; ++iEntry)
	{
		if (numData >= PoseMatchInfo::kMaxEntries)
			break;

		if (!matchInfo.m_entries[iEntry].m_valid)
			break;

		const CompressedChannel* pChannel = FindChannel(pAnim, matchInfo.m_entries[iEntry].m_channelId);
		if (!pChannel)
			continue;

		const CompressedChannel* pChannelLs = pChannel;

		if (numSourceSamples < 0)
			numSourceSamples = pChannelLs->m_numSamples;
		else if (numSourceSamples != pChannelLs->m_numSamples) // mismatched number of samples... nothing to do
			return -1.0f;

		data[numData].m_channelId = matchInfo.m_entries[iEntry].m_channelId;
		data[numData].m_pChannel = pChannel;
		data[numData].m_matchPos = Rotate(groundRotAdjust, matchInfo.m_entries[iEntry].m_matchPosOs - kOrigin) + Point(kOrigin);

		data[numData].m_velValid = matchInfo.m_entries[iEntry].m_velocityValid;
		data[numData].m_matchVel = Rotate(groundRotAdjust, matchInfo.m_entries[iEntry].m_matchVel);

		++numData;
	}

	const bool looping = pAnim->m_flags & ArtItemAnim::kLooping;
	const U32F minSamples = looping ? 2 : 1;

	if (numSourceSamples <= minSamples)
	{
		return PoseMatchResult(matchInfo.m_startPhase);
	}

	PMDebugPrint("numData %d\n", int(numData));

	// no valid channels
	if (numData == 0)
		return PoseMatchResult(-1.0f);

	if (matchInfo.m_rateRelativeToEntry0 && numData <= 1)
		return PoseMatchResult(0.0f); // can only rate relative to itself, so everything is awesome

	const bool inverted = matchInfo.m_endPhase < matchInfo.m_startPhase;

	const I32F firstSample = inverted ? (int)ceil((float)(numSourceSamples - 1) * matchInfo.m_startPhase)
									  : (int)floor((float)(numSourceSamples - 1) * matchInfo.m_startPhase);

	const I32F lastSample = inverted ? (int)floor((float)(numSourceSamples - 1) * matchInfo.m_endPhase) 
									 : (int)ceil((float)(numSourceSamples - 1) * matchInfo.m_endPhase);

	if (firstSample == lastSample)
	{
		return PoseMatchResult(matchInfo.m_startPhase);
	}

	PMDebugPrint("phase range [%f : %f]%s\n", matchInfo.m_startPhase, matchInfo.m_endPhase, inverted ? " (INVERTED)" : "");
	PMDebugPrint("sample range [%d : %d] numSourceSamples: %d\n", int(firstSample), int(lastSample), numSourceSamples);

	const float invNumSourceSamples = 1.0f / (numSourceSamples-1);

	float bestRating = kLargeFloat;
	float bestPhase = 0.0f;
	const float invCumulativeT = matchInfo.m_rateRelativeToEntry0 ? (1.0f / float(numData-1)) : (1.0f / float(numData));

	ScopedTempAllocator scopedTemp(FILE_LINE_FUNC);
	JointSampleCache sampleCache;

	sampleCache.Allocate(numData, numSourceSamples, firstSample, lastSample);

	sampleCache.Build(data, matchInfo, pAnim);
#if !FINAL_BUILD
	if (FALSE_IN_FINAL_BUILD(debug))
	{
		sampleCache.DebugDraw(ps);
	}
#endif
	U32F prevSample = firstSample;
	U32F sample = firstSample;
	
	do
	{
		sample = (sample + 1) % numSourceSamples;

		if (looping && (prevSample == numSourceSamples - 1) && (sample == 0))
		{
			// looping means the very last sample and the very first sample are the same data, so advance one extra
			sample = (sample + 1) % numSourceSamples;
		}

		float cumulativeT = 0.0f;

		// calculate the t to minimize the sum of the squares of the distances between the feet positions
		for (U32F iData = 0; iData < numData; ++iData)
		{
			const ndanim::JointParams& jp0 = sampleCache.Read(iData, prevSample);
			const ndanim::JointParams& jp1 = sampleCache.Read(iData, sample);

			if (matchInfo.m_rateRelativeToEntry0 && iData == 0)
				continue;

			if (matchInfo.m_rateRelativeToEntry0)
			{
				const ndanim::JointParams& jp00 = sampleCache.Read(0, prevSample);
				const ndanim::JointParams& jp01 = sampleCache.Read(0, sample);

				const Point m = (data[iData].m_matchPos - data[0].m_matchPos) + Point(kOrigin);
				const Point p0 = jp0.m_trans - jp00.m_trans + Point(kOrigin);
				const Point p1 = jp1.m_trans - jp01.m_trans + Point(kOrigin);
				Point closestPos = p0;
				Scalar unweightedT = 0.0f;
#if !FINAL_BUILD
				if (false) // (FALSE_IN_FINAL_BUILD(debug))
				{
					ps.DrawLine(p0, p1, kColorOrangeTrans);
					ps.DrawCross(p0, 0.05f, kColorOrangeTrans);
					ps.DrawString(p0, StringBuilder<64>("%d", sample).c_str(), kColorOrangeTrans, 0.5f);
					ps.DrawSphere(m, 0.025f, kColorOrangeTrans);
				}
#endif
				const float dist = DistPointSegment(m, p0, p1, &closestPos, &unweightedT);
				cumulativeT += unweightedT;
			}
			else
			{
				const Point m = data[iData].m_matchPos;
				const Point p0 = jp0.m_trans;
				const Point p1 = jp1.m_trans;
				Point closestPos = p0;
				Scalar unweightedT = 0.0f;

				const float dist = DistPointSegment(m, p0, p1, &closestPos, &unweightedT);
				cumulativeT += unweightedT;

				if (FALSE_IN_FINAL_BUILD(debug))
				{
					//ps.DrawLine(closestPos, m, kColorBlueTrans);
					//ps.DrawLine(p0, p1, kColorWhiteTrans, kColorGreen);
				}
			}
		}

		// next as a separate step, rate this segment as a distance from the average T to our match position
		float rating = 0.0f;

		const float avgT = cumulativeT * invCumulativeT;
		for (U32F iData = 0; iData < numData; ++iData)
		{
			if (matchInfo.m_rateRelativeToEntry0 && iData == 0)
				continue;

			const ndanim::JointParams& jp0 = sampleCache.Read(iData, prevSample);
			const ndanim::JointParams& jp1 = sampleCache.Read(iData, sample);
			
			if (matchInfo.m_rateRelativeToEntry0)
			{
				const ndanim::JointParams& jp00 = sampleCache.Read(0, prevSample);
				const ndanim::JointParams& jp01 = sampleCache.Read(0, sample);

				const Vector mVec = data[iData].m_matchPos - data[0].m_matchPos;
				const Vector v0 = jp0.m_trans - jp00.m_trans;
				const Vector v1 = jp1.m_trans - jp01.m_trans;
				const Vector rateVec = Lerp(v0, v1, avgT);
				const Point ratePos = Lerp(jp0.m_trans, jp1.m_trans, avgT);

				if (data[iData].m_velValid && !g_animOptions.m_disableAnkleVelMatching)
				{
					const Vector vel00 = sampleCache.ReadVelocity(0, prevSample);
					const Vector vel01 = sampleCache.ReadVelocity(0, sample);
					const Vector vel0 = sampleCache.ReadVelocity(iData, prevSample);
					const Vector vel1 = sampleCache.ReadVelocity(iData, sample);

					const Vector relV0 = vel00 + vel0;
					const Vector relV1 = vel01 + vel1;

					const Vector velDir0 = SafeNormalize(Lerp(vel00, vel01, avgT), kZero);
					const Vector velDir = SafeNormalize(Lerp(vel0, vel1, avgT), kZero);
					
					const Vector matchVelDir0 = SafeNormalize(data[0].m_matchVel, kZero);
					const Vector matchVelDir = SafeNormalize(data[iData].m_matchVel, kZero);

					const Quat velDelta = QuatFromVectors(velDir0, velDir);
					const Quat matchDelta = QuatFromVectors(matchVelDir0, matchVelDir);

					const float velDotP = Abs(Dot(velDelta, matchDelta)) * Min(DotXz(velDir, matchVelDir), DotXz(velDir0, matchVelDir0));
					const float velocityRating = 0.5f * Cube(Sqrt(Abs(velDotP)) * Sign(velDotP));

					const Vector mDir = SafeNormalize(mVec, kZero);
					const Vector rateDir = SafeNormalize(rateVec, kZero);

					const float dotP = Dot(mDir, rateDir);
					const float dotRating = dotP * dotP * dotP; //Sqr(dotP) * Sign(dotP);
					const float interimBoon = ((avgT > kSmallestFloat) && (avgT < (1.0f - kSmallestFloat))) ? (-0.25f * Sqr(dotRating)) : 0.0f;
					const float ratingSample = 15.0f - (12.0f * dotRating) - velocityRating + interimBoon;
					rating += ratingSample;
#ifndef FINAL_BUILD
					if (FALSE_IN_FINAL_BUILD(debug))
					{
						Color c0 = kColorCyan;
						Color c1 = kColorMagenta;
						c0.SetA(Sqr(dotRating));
						c1.SetA(Sqr(dotRating));
						const Point pos = Lerp(jp00.m_trans, jp01.m_trans, avgT);
						//ps.DrawLine(pos, pos + rateVec, c0, c1);
						ps.DrawString(pos, StringBuilder<128>("%d: dr:%0.3f + velr:%0.3f + ib:%0.3f = %0.3f", int(sample), dotRating, velocityRating, interimBoon, ratingSample).c_str(), kColorWhite, 0.5f);
					}
#endif
				}
				else
				{
					const float dotRating = Limit01(Dot(Normalize(rateVec), Normalize(mVec)));
					const float ratingSample = 1.0f / Max(dotRating, kSmallestFloat);
					rating += ratingSample;
				}
			}
			else
			{
				float velocityRating = 0.0f;
				Vector velDir = kZero;
				if (data[iData].m_velValid && !g_animOptions.m_disableAnkleVelMatching)
				{
					const Vector v0 = sampleCache.ReadVelocity(iData, prevSample);
					const Vector v1 = sampleCache.ReadVelocity(iData, sample);
					velDir = SafeNormalize(Lerp(v0, v1, avgT), kZero);
					const Vector matchV = SafeNormalize(data[iData].m_matchVel, kZero);
					const Scalar dotP = DotXz(velDir, matchV);
					velocityRating = dotP; //Sqrt(dotP);
				}

				const Point m = data[iData].m_matchPos;
				const Point p0 = jp0.m_trans;
				const Point p1 = jp1.m_trans;
				const Point ratePos = Lerp(p0, p1, avgT);
				const float baseRating = DistXzSqr(ratePos, m) + Abs(ratePos.Y() - m.Y());
				const float ratingSample = baseRating - velocityRating;
				rating += ratingSample;
			}
		}

		const float lastPhase = float(prevSample) * invNumSourceSamples;
		const float thisPhase = float(sample) * invNumSourceSamples;
		const float samplePhase = LoopingPhaseLerp(lastPhase, thisPhase, avgT);
		// match phase limits may lay not on exact sample boundaries, so quietly clamp here
		const float phaseToUse = Limit(samplePhase, matchInfo.m_startPhase, matchInfo.m_endPhase);

		PMDebugPrint("sample: %d (%f) -> %f\n", int(sample), samplePhase, rating);

		if (rating < bestRating)
		{
			bestRating = rating;
			bestPhase = samplePhase;
			
			PMDebugPrint("new best phase: %f (rating %f) sample = %d \n", bestPhase, rating, int(sample));
		}

		if (pAlternatesOut && pAlternatesOut->m_numAlternates < PoseMatchAlternates::kMaxAlternates)
		{
			if (rating < pAlternatesOut->m_alternateThreshold)
			{
				PMDebugPrint("Logging alternate %d @ phase %f (rating %f) sample = %d\n", pAlternatesOut->m_numAlternates, phaseToUse, rating, int(sample));

				pAlternatesOut->m_alternatePhase[pAlternatesOut->m_numAlternates] = phaseToUse;
				pAlternatesOut->m_alternateRating[pAlternatesOut->m_numAlternates] = rating;

				++pAlternatesOut->m_numAlternates;
			}
		}

		if (bestRating < matchInfo.m_earlyOutThreshold && matchInfo.m_earlyOutThreshold > 0.0f)
		{
			PMDebugPrint("Early out with rating %f below threshold of %f\n", bestRating, matchInfo.m_earlyOutThreshold);
			break;
		}

		prevSample = sample;
	}
	while (sample != lastSample);
	
	PMDebugPrint("returning best phase: %f\n", bestPhase);
#ifndef FINAL_BUILD
	if (FALSE_IN_FINAL_BUILD(debug))
	{
		for (U32F iData = 0; iData < numData; ++iData)
		{
			const float numFrameIntervals = float(numSourceSamples - 1);
			const float si = bestPhase * numFrameIntervals;
			const float s0 = Floor(bestPhase * numFrameIntervals);
			const float s1 = Ceil(bestPhase * numFrameIntervals);
			const float alpha = si - s0;

			const U32F is0 = U32F(s0);
			const U32F is1 = U32F(s1);

			const ndanim::JointParams& jp0 = sampleCache.Read(iData, is0);
			const ndanim::JointParams& jp1 = sampleCache.Read(iData, is1);

			const Point bestPosOs = Lerp(jp0.m_trans, jp1.m_trans, alpha);
			ps.DrawCross(bestPosOs, 0.025f, iData ? kColorMagenta : kColorCyan);
			ps.DrawCross(data[iData].m_matchPos, 0.025f, kColorRed);

			if (data[iData].m_velValid)
			{
				ps.DrawArrow(data[iData].m_matchPos, data[iData].m_matchVel * 0.15f, 0.05f, kColorRedTrans);
				ps.DrawString(data[iData].m_matchPos + data[iData].m_matchVel * 0.15f, "input-vel", kColorRedTrans, 0.5f);
			}

			ps.DrawLine(bestPosOs, data[iData].m_matchPos, iData ? kColorMagenta : kColorCyan, kColorRed);

			const Vector v0 = sampleCache.ReadVelocity(iData, is0);
			const Vector v1 = sampleCache.ReadVelocity(iData, is1);
			const Vector velDir = SafeNormalize(Lerp(v0, v1, alpha), kZero);
			ps.DrawArrow(bestPosOs, velDir * 0.11f, 0.1f, iData ? kColorMagenta : kColorCyan);
			ps.DrawString(bestPosOs + velDir * 0.11f, "best-vel", iData ? kColorMagenta : kColorCyan, 0.5f);
		}
	}
#endif
	if (inverted)
	{
		if (bestPhase <= matchInfo.m_endPhase)
		{
			bestPhase = Limit(bestPhase, 0.0f, matchInfo.m_endPhase);
		}
		else
		{
			bestPhase = Limit(bestPhase, matchInfo.m_startPhase, 1.0f);
		}
	}
	else
	{
		bestPhase = Limit(bestPhase, matchInfo.m_startPhase, matchInfo.m_endPhase);
	}

	PoseMatchResult result;
	result.m_phase = bestPhase;
	result.m_rating = bestRating;

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static Vec2 ToScreenCoord(float frame,
						  float totalFrames,
						  float value,
						  float lowest,
						  float highest,
						  float startX,
						  float endX,
						  float startY,
						  float endY)
{
	ANIM_ASSERT(totalFrames > 0);

	float x_factor = MinMax01(frame / totalFrames);
	float x_final = Lerp(startX, endX, x_factor);

	float y_raw = MinMax01((value - lowest) / (highest - lowest));	
	float y_final = Lerp(startY, endY, y_raw);

	return Vec2(x_final, y_final);
}

#define Diff2ScreenCoord(frame, totalFrames, value, lowest, highest) ToScreenCoord(frame, totalFrames, value, lowest, highest, 0.2f, 0.8f, 0.6f, 0.25f)
#define Y2ScreenCoord(frame, totalFrames, value, lowest, highest) ToScreenCoord(frame, totalFrames, value, lowest, highest, 0.03f, 0.3f, 0.6f, 0.25f)
#define Speed2ScreenCoord(frame, totalFrames, value, lowest, highest) ToScreenCoord(frame, totalFrames, value, lowest, highest, 0.36f, 0.63f, 0.6f, 0.25f)

/// --------------------------------------------------------------------------------------------------------------- ///-------------
static void Contact2ScreenCoord(const FootStateSegment& seg,
								const I32 totalFrames,
								Vec2* outP0,
								Vec2* outP1,
								float startX,
								float endX,
								float startY,
								float endY)
{
	const FootState state = seg.m_footState;
	const I32 frame0 = seg.m_startFrame;
	const I32 frame1 = seg.m_endFrame;

	if (state == kHeelToePlanted || state == kHeelToeInAir)
	{
		*outP0 = ToScreenCoord(frame0, totalFrames, state, kFootInvalid, kHeelToeInAir, startX, endX, startY, endY);
		*outP1 = ToScreenCoord(frame1, totalFrames, state, kFootInvalid, kHeelToeInAir, startX, endX, startY, endY);
	}
	else if (state == kToePlanted)
	{
		*outP0 = ToScreenCoord(frame0, totalFrames, kHeelToePlanted, kFootInvalid, kHeelToeInAir, startX, endX, startY, endY);
		*outP1 = ToScreenCoord(frame1, totalFrames, kHeelToeInAir, kFootInvalid, kHeelToeInAir, startX, endX, startY, endY);
	}
	else if (state == kHeelPlanted)
	{
		*outP0 = ToScreenCoord(frame0, totalFrames, kHeelToeInAir, kFootInvalid, kHeelToeInAir, startX, endX, startY, endY);
		*outP1 = ToScreenCoord(frame1, totalFrames, kHeelToePlanted, kFootInvalid, kHeelToeInAir, startX, endX, startY, endY);
	}
}

#define Contact2ScreenCoord0(seg, totalFrames, outP0, outP1) Contact2ScreenCoord(seg, totalFrames, outP0, outP1, 0.69f, 0.96f, 0.45f, 0.25f);
#define Contact2ScreenCoord1(seg, totalFrames, outP0, outP1) Contact2ScreenCoord(seg, totalFrames, outP0, outP1, 0.69f, 0.96f, 0.75f, 0.55f);

/// --------------------------------------------------------------------------------------------------------------- ///-------------
static bool DetermineFootOnGroundContact(const Transform& tCurrJoint, Vector_arg currFrameVec, const float yThreshold, const float velThreshold)
{
	const Locator currJointLoc(tCurrJoint);	

	float jointY = currJointLoc.GetTranslation().Y();
	float jointSpeedPerSecSqr = LengthSqr(currFrameVec);

	if (jointY < yThreshold && jointSpeedPerSecSqr < Sqr(velThreshold))
		return true;

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static FootState DetermineFootState(const Transform& ankleJointTrans,
									Vector_arg ankleJointVecPerSec,
									const Transform& toeJointTrans,
									Vector_arg toeJointVecPerSec,
									FootState prevState,
									const float ankleGroundYThres,
									const float toeGroundYThres,
									const float ankleLiftingVelThres,
									const float ankleLandingVelThres,
									const float toeLiftingVelThres,
									const float toeLandingVelThres)
{
	bool heelPlanted = false;
	bool toePlanted = false;

	if (prevState == kHeelToePlanted || prevState == kToePlanted || prevState == kFootInvalid)
	{
		// use smaller vel threshold!
		heelPlanted = DetermineFootOnGroundContact(ankleJointTrans, ankleJointVecPerSec, ankleGroundYThres, ankleLiftingVelThres);
		toePlanted = DetermineFootOnGroundContact(toeJointTrans, toeJointVecPerSec, toeGroundYThres, toeLiftingVelThres);
	}
	else if (prevState == kHeelToeInAir || prevState == kHeelPlanted)
	{
		// use bigger vel threshold!
		heelPlanted = DetermineFootOnGroundContact(ankleJointTrans, ankleJointVecPerSec, ankleGroundYThres, ankleLandingVelThres);
		toePlanted = DetermineFootOnGroundContact(toeJointTrans, toeJointVecPerSec, toeGroundYThres, toeLandingVelThres);
	}	

	// second part.
	if (heelPlanted && toePlanted)
		return kHeelToePlanted;
	else if (toePlanted && !heelPlanted)
		return kToePlanted;
	else if (heelPlanted && !toePlanted)
		return kHeelPlanted;
	else if (!heelPlanted && !toePlanted)
		return kHeelToeInAir;

	return kFootInvalid;
}

/// --------------------------------------------------------------------------------------------------------------- ///-------------
static void AppendNewFootStateSegment(const U32 numTotalFrames,
									  const I32 iFoot,
									  SingleFootSegmentInfo& rightFootInfo,
									  SingleFootSegmentInfo& leftFootInfo,
									  FootState lastFootState,
									  I32 lastSegmentEndFrame,
									  I32 newStartFrame)
{
	// add one new segment.
	FootStateSegment* pNewFootSegment = nullptr;
	if (iFoot == 0)
	{
		ANIM_ASSERT(rightFootInfo.m_numSegments < SingleFootSegmentInfo::kMaxNumFootSegments);
		pNewFootSegment = &rightFootInfo.m_footSegments[rightFootInfo.m_numSegments];
		rightFootInfo.m_numSegments++;
	}
	else
	{
		ANIM_ASSERT(leftFootInfo.m_numSegments < SingleFootSegmentInfo::kMaxNumFootSegments);
		pNewFootSegment = &leftFootInfo.m_footSegments[leftFootInfo.m_numSegments];
		leftFootInfo.m_numSegments++;
	}

	pNewFootSegment->m_footState = lastFootState;
	pNewFootSegment->m_startFrame = lastSegmentEndFrame;
	pNewFootSegment->m_endFrame = newStartFrame - 1;
}

static const I32F kMaxNumFoots = 2;

/// --------------------------------------------------------------------------------------------------------------- ///
struct SingleFrameJointTrans
{
public:
	static const I32F kMaxNumJoints = 8;	

	static Color GetDebugColor(I32F index) 
	{
		ANIM_ASSERT(index >= 0 && index < kMaxNumJoints); 
		static const Color kDebugColors[kMaxNumJoints] = 
		{
			Color(kColorGreen), Color(kColorBlue), Color(kColorRed), Color(kColorYellow), Color(kColorWhite), Color(kColorWhite), Color(kColorWhite), Color(kColorWhite),
		};

		return kDebugColors[index];
	}

	Transform m_transforms[kMaxNumJoints];
	Vector m_jointVecPerSec[kMaxNumJoints];	
};

/// --------------------------------------------------------------------------------------------------------------- ///
static void SampleJointDelta(const ArtItemAnim* pAnim,
							 const SkeletonId skelId,
							 const ArtItemSkeleton* pArtSkeleton,
							 StringId64 worldGroundApRef,
							 const U32* jointIndices,
							 const I32 numJoints,
							 const U32F numTotalFrames,
							 SingleFrameJointTrans* helpers)
{
	const bool bLooping = pAnim->m_flags & ArtItemAnim::kLooping;

	// pre-cache all joint transforms.
	for (U32F iFrame = 0; iFrame < numTotalFrames; iFrame++)
	{
		Locator align;
		float phase = (float)iFrame / numTotalFrames;
		bool valid = FindAlignFromApReference(skelId, pAnim, phase, Locator(kOrigin), worldGroundApRef, &align);

		SingleFrameJointTrans& eachHelper = helpers[iFrame];

		AnimateJoints(Transform(align.GetRotation(), align.GetTranslation()),
					  pArtSkeleton,
					  pAnim,
					  iFrame,
					  jointIndices,
					  numJoints,
					  eachHelper.m_transforms,
					  nullptr,
					  nullptr,
					  nullptr);
	}

	// calculate joint velocity.
	for (U32F iFrame = 1; iFrame < numTotalFrames - 1; iFrame++)
	{
		// get related frame. looping and transitional anims are different.
		I32F currFrame = iFrame;
		I32F prevFrame = currFrame - 1;
		I32F nextFrame = currFrame + 1;
		ANIM_ASSERT(prevFrame >= 0 && prevFrame < numTotalFrames);
		ANIM_ASSERT(nextFrame >= 0 && nextFrame < numTotalFrames);

		for (U32F iJoint = 0; iJoint < numJoints; iJoint++)
		{
			const Transform& prevJointTransform = helpers[prevFrame].m_transforms[iJoint];
			const Transform& currJointTransform = helpers[currFrame].m_transforms[iJoint];
			const Transform& nextJointTransform = helpers[nextFrame].m_transforms[iJoint];

			const Locator jointLocDelta0 = Locator(prevJointTransform).UntransformLocator(Locator(currJointTransform));
			const Locator jointLocDelta1 = Locator(currJointTransform).UntransformLocator(Locator(nextJointTransform));

			const Vector jointDelta0 = jointLocDelta0.GetTranslation() - kOrigin;
			const Vector jointDelta1 = jointLocDelta1.GetTranslation() - kOrigin;
			const Vector lerpedJointDelta = Lerp(jointDelta0, jointDelta1, 0.5f);
			const float jointDeltaLen = Length(lerpedJointDelta);
			const float jointSpeedPerSec = jointDeltaLen * pAnim->m_pClipData->m_framesPerSecond;
			const Vector jointVecPerSec = jointDeltaLen > 0.0f ? SafeNormalize(lerpedJointDelta, kZero) * jointSpeedPerSec : kZero;

			helpers[currFrame].m_jointVecPerSec[iJoint] = jointVecPerSec;
		}
	}

	// special case for single frame anim.
	if (numTotalFrames == 1)
	{
		for (U32F iJoint = 0; iJoint < numJoints; iJoint++)
		{
			helpers[0].m_jointVecPerSec[iJoint] = kZero;
		}
		return;
	}

	// process frame(0) and frame(numTotalFrames - 1)
	if (bLooping)
	{
		// looping!
		// remember! looping anim, frame(0) == frame(numTotalFrames - 1)		
		{
			// frame(0) and frame(numTotalFrames-1) are the same.
			I32F prevFrame = numTotalFrames - 2;
			I32F currFrame0 = numTotalFrames - 1;
			I32F currFrame1 = 0;
			I32F nextFrame = 1;

			for (U32F iJoint = 0; iJoint < numJoints; iJoint++)
			{
				// what we need is relative speed.
				const Transform& prevJointTransform = helpers[prevFrame].m_transforms[iJoint];
				const Transform& currJointTransform0 = helpers[currFrame0].m_transforms[iJoint];
				const Transform& currJointTransform1 = helpers[currFrame1].m_transforms[iJoint];
				const Transform& nextJointTransform = helpers[nextFrame].m_transforms[iJoint];

				const Locator jointLocDelta0 = Locator(prevJointTransform).UntransformLocator(Locator(currJointTransform0));
				const Locator jointLocDelta1 = Locator(currJointTransform1).UntransformLocator(Locator(nextJointTransform));

				const Vector jointDelta0 = jointLocDelta0.GetTranslation() - kOrigin;
				const Vector jointDelta1 = jointLocDelta1.GetTranslation() - kOrigin;
				const Vector lerpedJointDelta = Lerp(jointDelta0, jointDelta1, 0.5f);
				const float jointDeltaLen = Length(lerpedJointDelta);
				const float jointSpeedPerSec = jointDeltaLen * pAnim->m_pClipData->m_framesPerSecond;
				const Vector jointVecPerSec = jointDeltaLen > 0.0f ? SafeNormalize(lerpedJointDelta, kZero) * jointSpeedPerSec : kZero;

				helpers[0].m_jointVecPerSec[iJoint] = jointVecPerSec;
				helpers[numTotalFrames - 1].m_jointVecPerSec[iJoint] = jointVecPerSec;
			}			
		}
	}
	else
	{
		// transitional!
		{
			// frame(0)			
			I32F prevFrame = 0;
			I32F currFrame = 1;

			for (U32F iJoint = 0; iJoint < numJoints; iJoint++)
			{
				const Transform& prevJointTransform = helpers[prevFrame].m_transforms[iJoint];
				const Transform& currJointTransform = helpers[currFrame].m_transforms[iJoint];

				const Locator jointLocDelta0 = Locator(prevJointTransform).UntransformLocator(Locator(currJointTransform));
				const Vector jointDelta0 = jointLocDelta0.GetTranslation() - kOrigin;
				const float jointDeltaLen = Length(jointDelta0);
				const float jointSpeedPerSec = jointDeltaLen * pAnim->m_pClipData->m_framesPerSecond;
				const Vector jointVecPerSec = jointDeltaLen > 0.0f ? SafeNormalize(jointDelta0, kZero) * jointSpeedPerSec : kZero;

				helpers[0].m_jointVecPerSec[iJoint] = jointVecPerSec;
			}
		}

		{
			// frame(numTotalFrames - 1)
			I32F currFrame = numTotalFrames - 2;
			I32F nextFrame = numTotalFrames - 1;

			for (U32F iJoint = 0; iJoint < numJoints; iJoint++)
			{
				const Transform& currJointTransform = helpers[currFrame].m_transforms[iJoint];
				const Transform& nextJointTransform = helpers[nextFrame].m_transforms[iJoint];

				const Locator jointLocDelta1 = Locator(currJointTransform).UntransformLocator(Locator(nextJointTransform));
				const Vector jointDelta1 = jointLocDelta1.GetTranslation() - kOrigin;
				const float jointDeltaLen = Length(jointDelta1);
				const float jointSpeedPerSec = jointDeltaLen * pAnim->m_pClipData->m_framesPerSecond;
				const Vector jointVecPerSec = jointDeltaLen > 0.0f ? SafeNormalize(jointDelta1, kZero) * jointSpeedPerSec : kZero;

				helpers[numTotalFrames - 1].m_jointVecPerSec[iJoint] = jointVecPerSec;
			}
		}
	}
}

FootContactFuncParams::FootContactFuncParams()
	: m_skelId(0)
	, m_pAnim(nullptr)	
	, m_debugDraw(false)	
{
	m_groundApRef = SID("apReference");

	m_anklesGroundYThreshold[0] = 0.15f;
	m_anklesGroundYThreshold[1] = 0.16f;

	m_toesGroundYThreshold[0] = 0.09f;
	m_toesGroundYThreshold[1] = 0.1f;

	m_ankleLiftingVelThreshold = 0.4f;
	m_ankleLandingVelThreshold = 1.0f;
	m_toeLiftingVelThreshold = 0.4f;
	m_toeLandingVelThreshold = 0.8f;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool AnalyzeFootContact2(const FootContactFuncParams& params,
						 SingleFootSegmentInfo* outRightFootInfo,
						 SingleFootSegmentInfo* outLeftFootInfo)
{
	ANIM_ASSERT(outRightFootInfo != nullptr);
	ANIM_ASSERT(outLeftFootInfo != nullptr);

	const ArtItemSkeleton* pArtSkeleton = ResourceTable::LookupSkel(params.m_skelId).ToArtItem();
	const bool bLooping = params.m_pAnim->m_flags & ArtItemAnim::kLooping;
	const U32F numTotalFrames = params.m_pAnim->m_pClipData->m_numTotalFrames;
	if (numTotalFrames <= 0)
		return false;

	const U32F numTotalJoints = pArtSkeleton->m_numTotalJoints;
	const I32F rAnkleIndex = FindJoint(pArtSkeleton->m_pJointDescs, numTotalJoints, SID("r_ankle"));
	const I32F lAnkleIndex = FindJoint(pArtSkeleton->m_pJointDescs, numTotalJoints, SID("l_ankle"));
	const I32F rToeIndex = FindJoint(pArtSkeleton->m_pJointDescs, numTotalJoints, SID("r_toe"));
	const I32F lToeIndex = FindJoint(pArtSkeleton->m_pJointDescs, numTotalJoints, SID("l_toe"));

	if (rAnkleIndex < 0 || lAnkleIndex < 0 || rToeIndex < 0 || lToeIndex < 0)
		return false;

	static const I32F kMaxNumJoints = 4;

	const U32 jointIndices[kMaxNumJoints] = { static_cast<U32>(rAnkleIndex),
											  static_cast<U32>(rToeIndex),
											  static_cast<U32>(lAnkleIndex),
											  static_cast<U32>(lToeIndex) };
	const I32 numJoints = sizeof(jointIndices) / sizeof(jointIndices[0]);
	ANIM_ASSERT(numJoints == kMaxNumJoints);

	SingleFrameJointTrans* aJointTransHelper = STACK_ALLOC(SingleFrameJointTrans, numTotalFrames);
	memset(aJointTransHelper, 0, sizeof(SingleFrameJointTrans) * numTotalFrames);

	SampleJointDelta(params.m_pAnim,
					 params.m_skelId,
					 pArtSkeleton,
					 params.m_groundApRef,
					 jointIndices,
					 numJoints,
					 numTotalFrames,
					 aJointTransHelper);

	const float groundYThresholds[kMaxNumJoints] = 
	{ 
		params.m_anklesGroundYThreshold[0], params.m_toesGroundYThreshold[0],	// rAnkle, rToe
		params.m_anklesGroundYThreshold[1], params.m_toesGroundYThreshold[1],	// lAnkle, lToe
	};

	// second pass. determine the foot state based on ankle/toe state.
	FootState* footStates[kMaxNumFoots];
	for (U32F iFoot = 0; iFoot < kMaxNumFoots; iFoot++)
	{
		footStates[iFoot] = STACK_ALLOC(FootState, numTotalFrames);
		memset(footStates[iFoot], 0, sizeof(FootState) * numTotalFrames);
	}
	
	for (U32F iFoot = 0; iFoot < kMaxNumFoots; iFoot++)
	{
		FootState* eachFootStates = footStates[iFoot];
		const I32 ankleIndex = iFoot * 2;
		const I32 toeIndex = iFoot * 2 + 1;

		for (U32F iFrame = 0; iFrame < numTotalFrames; iFrame++)
		{
			const SingleFrameJointTrans& eachHelper = aJointTransHelper[iFrame];

			const Transform& ankleJointTrans = eachHelper.m_transforms[ankleIndex];
			const Transform& toeJointTrans = eachHelper.m_transforms[toeIndex];
			const Vector ankleJointVecPerSec = eachHelper.m_jointVecPerSec[ankleIndex];
			const Vector toeJointVecPerSec = eachHelper.m_jointVecPerSec[toeIndex];

			FootState prevState = iFrame > 0 ? eachFootStates[iFrame - 1] : kFootInvalid;
			const float ankleGroundYThreshold = params.m_anklesGroundYThreshold[iFoot];
			const float toeGroundYThreshold = params.m_toesGroundYThreshold[iFoot];

			eachFootStates[iFrame] = DetermineFootState(ankleJointTrans,
														ankleJointVecPerSec,
														toeJointTrans,
														toeJointVecPerSec,
														prevState,
														ankleGroundYThreshold,
														toeGroundYThreshold,
														params.m_ankleLiftingVelThreshold,
														params.m_ankleLandingVelThreshold,
														params.m_toeLiftingVelThreshold,
														params.m_toeLandingVelThreshold);
		}
	}

	// third pass. post-processing to fix some craziness.
	for (U32F iFoot = 0; iFoot < kMaxNumFoots; iFoot++)
	{
		FootState* eachFootStates = footStates[iFoot];
		
		for (U32F iFrame = 0; iFrame < numTotalFrames; iFrame++)
		{
			if (eachFootStates[iFrame] == kToePlanted)
			{
				I32 toePlantedStartFrame = iFrame;				
				I32 toePlantedEndFrame = iFrame;
				for (; toePlantedEndFrame < numTotalFrames; toePlantedEndFrame++)
				{
					if (eachFootStates[toePlantedEndFrame] != kToePlanted)
						break;
				}

				I32 numToePlantedFrames = toePlantedEndFrame - toePlantedStartFrame + 1;
				if (numToePlantedFrames < 3)
				{
					I32 prevFrame = toePlantedStartFrame - 1;
					I32 nextFrame = toePlantedEndFrame + 1;

					if (prevFrame >= 0 && nextFrame < numTotalFrames)
					{
						if (eachFootStates[prevFrame] == kHeelToeInAir && eachFootStates[nextFrame] == kHeelToeInAir)
						{
							// ok, catch some bad frames.
							for (U32F jFrame = toePlantedStartFrame; jFrame <= toePlantedEndFrame; jFrame++)
								eachFootStates[jFrame] = kHeelToeInAir;

							iFrame = toePlantedEndFrame + 1;
						}
						else if (eachFootStates[prevFrame] == kHeelToePlanted && eachFootStates[nextFrame] == kHeelToePlanted)
						{
							// ok, catch some bad frames.
							for (U32F jFrame = toePlantedStartFrame; jFrame <= toePlantedEndFrame; jFrame++)
								eachFootStates[jFrame] = kHeelToePlanted;

							iFrame = toePlantedEndFrame + 1;
						}
					}
				}
			}
		}
	}

#ifndef FINAL_BUILD	
	// draw the joint's y and velocity.
	if (params.m_debugDraw)
	{
		float lowestY = kLargestFloat;
		float highestY = -kLargestFloat;
		float lowestSpeed = kLargestFloat;
		float highestSpeed = -kLargestFloat;
	
		// find out the lowest/highest.
		for (U32F iFrame = 0; iFrame < numTotalFrames; iFrame++)
		{
			const SingleFrameJointTrans& eachHelper = aJointTransHelper[iFrame];
			for (U32F iJoint = 0; iJoint < numJoints; iJoint++)
			{
				const float jointTranslationY = eachHelper.m_transforms[iJoint].GetAxis(3).Y();
				if (jointTranslationY < lowestY)
					lowestY = jointTranslationY;
				if (jointTranslationY > highestY)
					highestY = jointTranslationY;
	
				const float jointSpeedPerSec = Length(eachHelper.m_jointVecPerSec[iJoint]);
				if (jointSpeedPerSec < lowestSpeed)
					lowestSpeed = jointSpeedPerSec;
				if (jointSpeedPerSec > highestSpeed)
					highestSpeed = jointSpeedPerSec;
			}
		}
	
		Vec2 lastY[kMaxNumJoints];
		Vec2 lastSpeed[kMaxNumJoints];		
	
		// draw debug axis.
		for (U32F iJoint = 0; iJoint < numJoints; iJoint++)
		{
			Color debugColor = SingleFrameJointTrans::GetDebugColor(iJoint);
			g_prim.Draw(DebugLine2D(Y2ScreenCoord(0, numTotalFrames, groundYThresholds[iJoint], lowestY, highestY), Y2ScreenCoord(numTotalFrames, numTotalFrames, groundYThresholds[iJoint], lowestY, highestY), kDebug2DNormalizedCoords, debugColor));
		}
		
		g_prim.Draw(DebugLine2D(Speed2ScreenCoord(0, numTotalFrames, params.m_ankleLiftingVelThreshold, lowestSpeed, highestSpeed), Speed2ScreenCoord(numTotalFrames, numTotalFrames, params.m_ankleLiftingVelThreshold, lowestSpeed, highestSpeed), kDebug2DNormalizedCoords, kColorGreen));
		g_prim.Draw(DebugLine2D(Speed2ScreenCoord(0, numTotalFrames, params.m_ankleLandingVelThreshold, lowestSpeed, highestSpeed), Speed2ScreenCoord(numTotalFrames, numTotalFrames, params.m_ankleLandingVelThreshold, lowestSpeed, highestSpeed), kDebug2DNormalizedCoords, kColorGreen));

		g_prim.Draw(DebugLine2D(Speed2ScreenCoord(0, numTotalFrames, params.m_toeLiftingVelThreshold, lowestSpeed, highestSpeed), Speed2ScreenCoord(numTotalFrames, numTotalFrames, params.m_toeLiftingVelThreshold, lowestSpeed, highestSpeed), kDebug2DNormalizedCoords, kColorBlue));
		g_prim.Draw(DebugLine2D(Speed2ScreenCoord(0, numTotalFrames, params.m_toeLandingVelThreshold, lowestSpeed, highestSpeed), Speed2ScreenCoord(numTotalFrames, numTotalFrames, params.m_toeLandingVelThreshold, lowestSpeed, highestSpeed), kDebug2DNormalizedCoords, kColorBlue));
	
		g_prim.Draw(DebugString2D(Y2ScreenCoord(0, numTotalFrames, highestY, lowestY, highestY), kDebug2DNormalizedCoords, "JointTransformY", kColorYellow, 0.8f));
		g_prim.Draw(DebugString2D(Speed2ScreenCoord(0, numTotalFrames, highestSpeed, lowestSpeed, highestSpeed), kDebug2DNormalizedCoords, "JointVel", kColorYellow, 0.8f));
	
		// frame 0.
		{
			const SingleFrameJointTrans& helper0 = aJointTransHelper[0];
			for (U32F iJoint = 0; iJoint < numJoints; iJoint++)
			{			
				float y = helper0.m_transforms[iJoint].GetAxis(3).Y();
				lastY[iJoint] = Y2ScreenCoord(0, numTotalFrames, y, lowestY, highestY);
				float jointSpeedPerSec = Length(helper0.m_jointVecPerSec[iJoint]);
				lastSpeed[iJoint] = Speed2ScreenCoord(0, numTotalFrames, jointSpeedPerSec, lowestSpeed, highestSpeed);
			}
		}
	
		I32 numFramesToShowText = numTotalFrames / 7;
	
		for (U32F iFrame = 1; iFrame < numTotalFrames; iFrame++)
		{
			const SingleFrameJointTrans& eachHelper = aJointTransHelper[iFrame];

			for (U32F iJoint = 0; iJoint < numJoints; iJoint++)
			{				
				float currY = eachHelper.m_transforms[iJoint].GetAxis(3).Y();
				float jointSpeedPerSec = Length(eachHelper.m_jointVecPerSec[iJoint]);

				Color debugColor = SingleFrameJointTrans::GetDebugColor(iJoint);
	
				// draw Y.
				Vec2 p0 = Y2ScreenCoord(iFrame, numTotalFrames, currY, lowestY, highestY);
				g_prim.Draw(DebugLine2D(lastY[iJoint], p0, kDebug2DNormalizedCoords, debugColor));
				lastY[iJoint] = p0;
	
				// draw ankle speed.
				Vec2 p1 = Speed2ScreenCoord(iFrame, numTotalFrames, jointSpeedPerSec, lowestSpeed, highestSpeed);
				g_prim.Draw(DebugLine2D(lastSpeed[iJoint], p1, kDebug2DNormalizedCoords, debugColor));
				lastSpeed[iJoint] = p1;
	
				if (iFrame % numFramesToShowText == 0)
				{
					g_prim.Draw(DebugString2D(Y2ScreenCoord(iFrame, numTotalFrames, lowestY, lowestY, highestY), kDebug2DNormalizedCoords, StringBuilder<64>("%d", iFrame).c_str(), kColorYellow, 0.7f));
					g_prim.Draw(DebugString2D(Speed2ScreenCoord(iFrame, numTotalFrames, lowestSpeed, lowestSpeed, highestSpeed), kDebug2DNormalizedCoords, StringBuilder<64>("%d", iFrame).c_str(), kColorYellow, 0.7f));
				}
			}
		}
	}
#endif

	I32 lastSegmentEndFrame[kMaxNumFoots] = { 0, 0 };
	
	for (U32F iFoot = 0; iFoot < kMaxNumFoots; iFoot++)
	{
		const FootState* eachFootStates = footStates[iFoot];
		const I32 ankleIndex = iFoot * 2;
		const I32 toeIndex = iFoot * 2 + 1;

		for (U32F iFrame = 1; iFrame < numTotalFrames; iFrame++)
		{
			const FootState prevState = eachFootStates[iFrame - 1];
			const FootState currState = eachFootStates[iFrame];

			if (currState != prevState)
			{
				// add one new segment.
				AppendNewFootStateSegment(numTotalFrames, iFoot, *outRightFootInfo, *outLeftFootInfo,
					prevState, lastSegmentEndFrame[iFoot], iFrame);

				lastSegmentEndFrame[iFoot] = iFrame;
			}
		}
	}

	// add last segment if necessary.
	for (U32F iFoot = 0; iFoot < kMaxNumFoots; iFoot++)
	{
		if (numTotalFrames <= lastSegmentEndFrame[iFoot]) continue;

		const FootState* eachFootStates = footStates[iFoot];
		const FootState lastState = eachFootStates[lastSegmentEndFrame[iFoot]];

		// add one new segment.
		AppendNewFootStateSegment(numTotalFrames, iFoot, *outRightFootInfo, *outLeftFootInfo,
			lastState, lastSegmentEndFrame[iFoot], numTotalFrames);
	}

#ifndef FINAL_BUILD
	// final draw the result if requested,
	if (params.m_debugDraw)
	{
		Color debugColor[kMaxNumFoots] = { kColorGreen, kColorRed };		

		for (U32F ii = 0; ii < outRightFootInfo->m_numSegments; ii++)
		{
			const FootStateSegment& seg = outRightFootInfo->m_footSegments[ii];
	
			Vec2 p2_0, p2_1;
			Contact2ScreenCoord0(seg, numTotalFrames, &p2_0, &p2_1);
			g_prim.Draw(DebugString2D(p2_0, kDebug2DNormalizedCoords, StringBuilder<64>("%d", seg.m_startFrame).c_str(), debugColor[0], 0.7f));
			g_prim.Draw(DebugLine2D(p2_0, p2_1, kDebug2DNormalizedCoords, debugColor[0]));

			if (ii == 0)
			{
				Vec2 pp0, pp1;
				FootStateSegment textSeg = seg;
				textSeg.m_footState = kHeelToePlanted;
				Contact2ScreenCoord0(textSeg, numTotalFrames, &pp0, &pp1);
				pp0.y += 0.03f;
				g_prim.Draw(DebugString2D(pp0, kDebug2DNormalizedCoords, "Heel&Toe Planted", kColorYellow, 0.7f));

				textSeg.m_footState = kHeelToeInAir;
				Contact2ScreenCoord0(textSeg, numTotalFrames, &pp0, &pp1);
				pp0.y += 0.03f;
				g_prim.Draw(DebugString2D(pp0, kDebug2DNormalizedCoords, "Heel&Toe Planted", kColorYellow, 0.7f));
			}
		}
	
		for (U32F ii = 0; ii < outLeftFootInfo->m_numSegments; ii++)
		{
			const FootStateSegment& seg = outLeftFootInfo->m_footSegments[ii];
			
			Vec2 p2_0, p2_1;
			Contact2ScreenCoord1(seg, numTotalFrames, &p2_0, &p2_1);			
	
			g_prim.Draw(DebugString2D(p2_0, kDebug2DNormalizedCoords, StringBuilder<64>("%d", seg.m_startFrame).c_str(), debugColor[1], 0.7f));
			g_prim.Draw(DebugLine2D(p2_0, p2_1, kDebug2DNormalizedCoords, debugColor[1]));

			if (ii == 0)
			{
				Vec2 pp0, pp1;
				FootStateSegment textSeg = seg;
				textSeg.m_footState = kHeelToePlanted;
				Contact2ScreenCoord1(textSeg, numTotalFrames, &pp0, &pp1);
				pp0.y += 0.03f;
				g_prim.Draw(DebugString2D(pp0, kDebug2DNormalizedCoords, "Heel&Toe Planted", kColorYellow, 0.7f));

				textSeg.m_footState = kHeelToeInAir;
				Contact2ScreenCoord1(textSeg, numTotalFrames, &pp0, &pp1);
				pp0.y += 0.03f;
				g_prim.Draw(DebugString2D(pp0, kDebug2DNormalizedCoords, "Heel&Toe InAir", kColorYellow, 0.7f));
			}
		}
	}
#endif
	
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static void SampleJointTransforms(const AnimJointBuffer* pAnimJointBuffer,
								  const U32* aJointIndices,
								  const U32F numJoints,
								  const SkeletonId skelId,
								  const ArtItemSkeleton* pArtSkeleton,
								  const ArtItemAnim* pAnim,
								  StringId64 worldGroundApRef,
								  I32 frame,
								  ndanim::JointParams* outOCurrJointParams,
								  ndanim::JointParams* outRCurrJointParams,
								  ndanim::JointParams* outRPrevJointParams)
{
	const U16 numTotalFrames = pAnim->m_pClipData->m_numTotalFrames;
	ANIM_ASSERT(frame >= 0.0f && frame <= numTotalFrames);

	const bool bLooping = pAnim->m_flags & ArtItemAnim::kLooping;

	if (pAnimJointBuffer != nullptr)
	{
		memcpy(outOCurrJointParams, pAnimJointBuffer->GetFrameBuffer(frame).m_joints, sizeof(ndanim::JointParams) * numJoints);
	}
	else
	{
		Locator oAlign;
		float phase = (float)frame / numTotalFrames;
		bool valid = FindAlignFromApReference(skelId, pAnim, phase, Locator(kOrigin), worldGroundApRef, &oAlign);

		const Transform& oAlignTrans = Transform(oAlign.GetRotation(), oAlign.GetTranslation());

		AnimateJoints(oAlignTrans,
					  pArtSkeleton,
					  pAnim,
					  frame,
					  aJointIndices,
					  numJoints,
					  nullptr,
					  outOCurrJointParams,
					  nullptr,
					  nullptr);
	}

	// find prev frame of 2 anims.
	// for transitional anim, we trust the speed at frame 0 and frame (-1) is almost the same. frame(-1) is unobservable
	I32 currFrame = frame;

	if (currFrame == 0)
		currFrame = bLooping ? numTotalFrames - 1 : 1;
	float prevFrame = currFrame - 1;

	// handle single frame anim.
	if (numTotalFrames == 1)
	{
		currFrame = prevFrame = 0;
	}

	ANIM_ASSERT(currFrame >= 0.0f && currFrame < numTotalFrames && prevFrame >= 0.0f && prevFrame < numTotalFrames);

	// joint rotation velocity. 
	// if it's transitional anim, we don't have info before frame 0.
	// if it's looping anim, we have prev frame of frame0 at frame numTotalFrames - 2.

	if (currFrame == frame)
	{
		memcpy(outRCurrJointParams, outOCurrJointParams, sizeof(ndanim::JointParams) * numJoints);
	}
	else if (pAnimJointBuffer != nullptr)
	{
		memcpy(outRCurrJointParams, pAnimJointBuffer->GetFrameBuffer(currFrame).m_joints, sizeof(ndanim::JointParams) * numJoints);
	}
	else
	{
		Locator oAlign;
		float phase = (float)currFrame / numTotalFrames;
		bool valid = FindAlignFromApReference(skelId, pAnim, phase, Locator(kOrigin), worldGroundApRef, &oAlign);

		const Transform& oAlignTrans = Transform(oAlign.GetRotation(), oAlign.GetTranslation());
		AnimateJoints(oAlignTrans,
					  pArtSkeleton,
					  pAnim,
					  currFrame,
					  aJointIndices,
					  numJoints,
					  nullptr,
					  outRCurrJointParams,
					  nullptr,
					  nullptr);
	}

	if (pAnimJointBuffer != nullptr)
	{
		memcpy(outRPrevJointParams, pAnimJointBuffer->GetFrameBuffer(prevFrame).m_joints, sizeof(ndanim::JointParams) * numJoints);
	}
	else
	{
		Locator oAlign;
		float phase = (float)prevFrame / numTotalFrames;
		bool valid = FindAlignFromApReference(skelId, pAnim, phase, Locator(kOrigin), worldGroundApRef, &oAlign);

		const Transform& oAlignTrans = Transform(oAlign.GetRotation(), oAlign.GetTranslation());
		AnimateJoints(oAlignTrans,
					  pArtSkeleton,
					  pAnim,
					  prevFrame,
					  aJointIndices,
					  numJoints,
					  nullptr,
					  outRPrevJointParams,
					  nullptr,
					  nullptr);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
float ComputeJointDifference(const AnimJointBuffer* pAnimJointBufferI,
							 const AnimJointBuffer* pAnimJointBufferJ,
							 StringId64 groundApRef,
							 const JointDiffWeight* arrJointWeights,
							 const I32 numJoints,
							 const I16 rootIndex,
							 const float rootDeltaDiffWeight,
							 const SkeletonId skelId,
							 const ArtItemAnim* pAnimI,
							 const I32 frameI,
							 const ArtItemAnim* pAnimJ,
							 const I32 frameJ,
							 JointDiffResult* outResults,
							 I32* outNumResults,
							 const I32 maxNumResults)
{
	if (!numJoints)
		return kLargestFloat;

	const ArtItemSkeleton* pArtSkeleton = ResourceTable::LookupSkel(skelId).ToArtItem();
	const bool bLoopingI = pAnimI->m_flags & ArtItemAnim::kLooping;
	const bool bLoopingJ = pAnimJ->m_flags & ArtItemAnim::kLooping;
	const U16 totalFramesI = pAnimI->m_pClipData->m_numTotalFrames;
	const U16 totalFramesJ = pAnimJ->m_pClipData->m_numTotalFrames;

	ANIM_ASSERT(pAnimI->m_pClipData->m_numTotalFrames > 0);	// at least 2 frames.
	ANIM_ASSERT(pAnimJ->m_pClipData->m_numTotalFrames > 0);	// at least 2 frames.
	ANIM_ASSERT(frameI >= 0.0f && frameI <= totalFramesI);		// it's ok frameI == totalFramesI, it's the last frame.
	ANIM_ASSERT(frameJ >= 0.0f && frameJ <= totalFramesJ);		// it's ok frameJ == totalFramesJ, it's the last frame.
	//const U32F numTotalJoints = pArtSkeleton->m_pRsxSkel->m_numTotalJoints;	
	const bool needRotationSpeed = totalFramesI > 1 && totalFramesJ > 1;

	ANIM_ASSERT(rootIndex >= 0);
	ANIM_ASSERT(groundApRef != INVALID_STRING_ID_64);

	ScopedTempAllocator jj(FILE_LINE_FUNC);

	U32* aJointIndices = NDI_NEW U32[numJoints];
	// fill the indices.
	for (U32F iJoint = 0; iJoint < numJoints; iJoint++)
		aJointIndices[iJoint] = arrJointWeights[iJoint].m_jointIndex;

	// joint orientation.
	//Transform* aCurrJointTransII = NDI_NEW Transform[numTotalJoints];
	//Transform* aCurrJointTransJJ = NDI_NEW Transform[numTotalJoints];
	ndanim::JointParams* aCurrJointParamsII = NDI_NEW ndanim::JointParams[numJoints];
	ndanim::JointParams* aCurrJointParamsJJ = NDI_NEW ndanim::JointParams[numJoints];	
	ndanim::JointParams* aaCurrJointParamsII = NDI_NEW ndanim::JointParams[numJoints];
	ndanim::JointParams* aaCurrJointParamsJJ = NDI_NEW ndanim::JointParams[numJoints];
	ndanim::JointParams* aaPrevJointParamsII = NDI_NEW ndanim::JointParams[numJoints];
	ndanim::JointParams* aaPrevJointParamsJJ = NDI_NEW ndanim::JointParams[numJoints];
	SampleJointTransforms(pAnimJointBufferI, aJointIndices, numJoints, skelId, pArtSkeleton, pAnimI, groundApRef, frameI, aCurrJointParamsII, aaCurrJointParamsII, aaPrevJointParamsII);
	SampleJointTransforms(pAnimJointBufferJ, aJointIndices, numJoints, skelId, pArtSkeleton, pAnimJ, groundApRef, frameJ, aCurrJointParamsJJ, aaCurrJointParamsJJ, aaPrevJointParamsJJ);

	// d(Pi, Pj) = |Pi,0 - Pj,0|^2 + Sum(Wk * |log(Qj,k^-1 * Qi,k)|^2)
	// where Pi,0 is the translational position of the character at frame i, 
	// Qi,k is the orientation of joint k with respect to its parent in frame i.
	// and joint angle differences are summed over m rotational joints.
	// The value of log(Qa^-1 * Qb)is a vector V such that a rotation of 2|V| about the axis V/|V|, takes a body from orientation Qa to orientation Qb.
		
#define APPEND_JOINT_DIFF_RESULT(name, type, raw, final)									\
	if (outResults != nullptr && outNumResults != nullptr && (*outNumResults) < maxNumResults)	\
	{																						\
		JointDiffResult& newResult = outResults[(*outNumResults)++];						\
		newResult.m_name = name;															\
		newResult.m_type = type;															\
		newResult.m_diffRaw = raw;															\
		newResult.m_diffFinal = final;														\
	};

	// we don't use root transitional diff. if 
	//float rootTranslationDiffRaw = DistSqr(aCurrJointParamsII[rootIndex].m_trans, aCurrJointParamsJJ[rootIndex].m_trans);
	//float rootTranslationDiffFinal = rootTranslationDiffRaw * rootTranslationalDiffWeight;
	//APPEND_JOINT_DIFF_RESULT(SID("root"), SID("translation"), rootTranslationDiffRaw, rootTranslationDiffFinal);
	
	float rootDeltaDiffFinal = 0.0f;
	{
		Locator alignDeltaLocII, alignDeltaLocJJ;

		{
			float prevPhase = frameI > 0 ? (float)(frameI - 1) / totalFramesI : 0.0f;
			float currPhase = frameI > 0 ? (float)frameI / totalFramesI : 1.0f / totalFramesI;
			
			Locator prevAlignLoc;
			Locator currAlignLoc;

			bool valid0 = FindAlignFromApReference(skelId, pAnimI, prevPhase, Locator(kOrigin), groundApRef, &prevAlignLoc);
			bool valid1 = FindAlignFromApReference(skelId, pAnimI, currPhase, Locator(kOrigin), groundApRef, &currAlignLoc);
			alignDeltaLocII = prevAlignLoc.UntransformLocator(currAlignLoc);
		}
		
		{
			float prevPhase = frameJ > 0 ? (float)(frameJ - 1) / totalFramesJ : 0.0f;
			float currPhase = frameJ > 0 ? (float)frameJ / totalFramesJ : 1.0f / totalFramesJ;

			Locator prevAlignLoc;
			Locator currAlignLoc;

			bool valid0 = FindAlignFromApReference(skelId, pAnimJ, prevPhase, Locator(kOrigin), groundApRef, &prevAlignLoc);
			bool valid1 = FindAlignFromApReference(skelId, pAnimJ, currPhase, Locator(kOrigin), groundApRef, &currAlignLoc);
			alignDeltaLocJJ = prevAlignLoc.UntransformLocator(currAlignLoc);
		}
		
		const Vector alignDeltaNormalizedII = (alignDeltaLocII.GetTranslation() - kOrigin) / pAnimI->m_pClipData->m_secondsPerFrame / 30.f;
		const Vector alignDeltaNormalizedJJ = (alignDeltaLocJJ.GetTranslation() - kOrigin) / pAnimJ->m_pClipData->m_secondsPerFrame / 30.f;
		const Vector rootDelta = alignDeltaNormalizedII - alignDeltaNormalizedJJ;

		float rootDeltaDiffRaw = LengthSqr(rootDelta);
		rootDeltaDiffFinal = rootDeltaDiffRaw * rootDeltaDiffWeight;	// [TODO]: need a weight!
		APPEND_JOINT_DIFF_RESULT(SID("root"), SID("delta"), rootDeltaDiffRaw, rootDeltaDiffFinal);
	}

	float totalOrientationDiff = 0.0f;
	float totalRotationVelDiff = 0.0f;

	for (U32F iJoint = 0; iJoint < numJoints; iJoint++)
	{
		const JointDiffWeight& weight = arrJointWeights[iJoint];
		const StringId64 jointNameId = weight.m_jointNameId;
		
		const ndanim::JointParams& Qi = aCurrJointParamsII[iJoint];
		const ndanim::JointParams& Qj = aCurrJointParamsJJ[iJoint];

		// calculate orientation diff.
		const Quat Q_J_TO_I = Conjugate(Qj.m_quat) * Qi.m_quat;
		const Vec4 V_2 = Q_J_TO_I.GetVec4();

		Vector V(V_2.Get(0), V_2.Get(1), V_2.Get(2));
		V = V / 2.0f;
		float orientationDiffRaw = LengthSqr(V);
		float orientationDiffFinal = orientationDiffRaw * weight.m_orientationDiffWeight;
		APPEND_JOINT_DIFF_RESULT(jointNameId, SID("orientation"), orientationDiffRaw, orientationDiffFinal);

		float rotationVelDiffFinal = 0.0f;
		if (needRotationSpeed)
		{
			// calculate rotation velocity diff.
			const ndanim::JointParams& Q_curr_i = aaCurrJointParamsII[iJoint];
			const ndanim::JointParams& Q_curr_j = aaCurrJointParamsJJ[iJoint];
			const ndanim::JointParams& Q_prev_i = aaPrevJointParamsII[iJoint];
			const ndanim::JointParams& Q_prev_j = aaPrevJointParamsJJ[iJoint];			

			const Quat rotation_delta_i = Conjugate(Q_prev_i.m_quat) * Q_curr_i.m_quat;
			const Quat rotation_delta_j = Conjugate(Q_prev_j.m_quat) * Q_curr_j.m_quat;

			Vec4 rotation_axis_i(kZero), rotation_axis_j(kZero);
			float rotation_angle_i = 0.0f, rotation_angle_j = 0.0f;
			rotation_delta_i.GetAxisAndAngle(rotation_axis_i, rotation_angle_i);
			rotation_delta_j.GetAxisAndAngle(rotation_axis_j, rotation_angle_j);

			// normalize to delta_angle per second.
			const Quat rot_vel_i = Quat(rotation_axis_i, rotation_angle_i / pAnimI->m_pClipData->m_secondsPerFrame / 30.f);
			const Quat rot_vel_j = Quat(rotation_axis_j, rotation_angle_j / pAnimJ->m_pClipData->m_secondsPerFrame / 30.f);

			const Quat rotation_vel_j_to_i = Conjugate(rot_vel_j) * rot_vel_i;
			const Vec4 R_2 = rotation_vel_j_to_i.GetVec4();

			Vector R(R_2.Get(0), R_2.Get(1), R_2.Get(2));
			//R = R / 2.0f; // it turns out it's too small.
			float rotationVelDiffRaw = LengthSqr(R);
			rotationVelDiffFinal = rotationVelDiffRaw * weight.m_rotationVelDiffWeight;
			APPEND_JOINT_DIFF_RESULT(jointNameId, SID("rot_vel"), rotationVelDiffRaw, rotationVelDiffFinal);
		}

		totalOrientationDiff += orientationDiffFinal;
		totalRotationVelDiff += rotationVelDiffFinal;
	}

	return /*rootTranslationDiffFinal + */rootDeltaDiffFinal + totalOrientationDiff + totalRotationVelDiff;
}

/// --------------------------------------------------------------------------------------------------------------- ///
struct FrameDiffHelper
{
public:
	FrameDiffHelper() {}

	static I32 Compare(const FrameDiffHelper& a, const FrameDiffHelper& b)
	{
		if (a.m_diff < b.m_diff)
			return -1;
		else if (a.m_diff > b.m_diff)
			return +1;
		else
			return 0;
	}

	I32 m_frame;
	float m_diff;
};

/// --------------------------------------------------------------------------------------------------------------- ///
void ConvertJointWeights(StringId64 jointDiffDefId,
						 SkeletonId skelId,
						 float* outRootDeltaDiffWeight,
						 JointDiffWeight* outWeights,
						 I32* outNumWeights,
						 const I32 maxNumWeights)
{
	const DC::JointDiffWeightArray* settings = static_cast<const DC::JointDiffWeightArray*>(ScriptManager::LookupPointerInModule(jointDiffDefId, SID("pose-matching"), nullptr));
	if (!settings)
		return;

	ANIM_ASSERT(settings->m_count <= maxNumWeights);
	const ArtItemSkeleton* pArtSkeleton = ResourceTable::LookupSkel(skelId).ToArtItem();

	for (U32F iJoint = 0; iJoint < settings->m_count; iJoint++)
	{
		JointDiffWeight& newWeight = outWeights[iJoint];
		newWeight.m_jointNameId = settings->m_array[iJoint].m_jointName;
		newWeight.m_jointIndex = FindJoint(pArtSkeleton->m_pJointDescs, pArtSkeleton->m_numTotalJoints, newWeight.m_jointNameId);
		ANIM_ASSERTF(newWeight.m_jointIndex != -1, ("Can't find joint:%s on character, check %s\n", DevKitOnly_StringIdToString(newWeight.m_jointNameId), DevKitOnly_StringIdToString(jointDiffDefId)));
		newWeight.m_orientationDiffWeight = settings->m_array[iJoint].m_orientationDiffWeight;
		newWeight.m_rotationVelDiffWeight = settings->m_array[iJoint].m_rotationVelDiffWeight;
	}

	*outRootDeltaDiffWeight = settings->m_rootDeltaDiffWeight;
	*outNumWeights = settings->m_count;
}

/// --------------------------------------------------------------------------------------------------------------- ///
JointDiffFuncParams::JointDiffFuncParams()
	: m_jointDiffDefId(INVALID_STRING_ID_64)
	, m_skelId(0)
	, m_debugDraw(false)
	, m_printError(false)
	, m_pAnimI(nullptr)
	, m_pAnimJ(nullptr)
	, m_frameI(0)
	, m_minPhase(0.0f)
	, m_maxPhase(1.0f)
	, m_useLookupTable(false)
	, m_precacheAnimBufferI(nullptr)
	, m_precacheAnimBufferJ(nullptr)
{
	m_groundApRef = SID("apReference");
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ComputeJointDiffFrameInternal(const JointDiffFuncParams& params,
								   const SingleFootSegmentInfo* rightFootI,
								   const SingleFootSegmentInfo* leftFootI,
								   const SingleFootSegmentInfo* rightFootJ,
								   const SingleFootSegmentInfo* leftFootJ,
								   I32* outFrameJ)
{
	const I32 frameI = params.m_frameI;
	const ArtItemAnim* pAnimI = params.m_pAnimI;
	const ArtItemAnim* pAnimJ = params.m_pAnimJ;	
	const SkeletonId skelId = params.m_skelId;
	const bool debugDraw = params.m_debugDraw;

	if (frameI < 0 && frameI >= pAnimI->m_pClipData->m_numTotalFrames)
		return false;

	static const I32F sc_maxPossibleSegments = 64;
	FootStateSegment possibleSegments[sc_maxPossibleSegments];
	I32 numPossibleSegments = 0;

	const I32 numTotalFramesI = pAnimI->m_pClipData->m_numTotalFrames;
	const I32 numTotalFramesJ = pAnimJ->m_pClipData->m_numTotalFrames;

	const I32 minFrameJ = MinMax((const I32)(numTotalFramesJ * params.m_minPhase), 0, numTotalFramesJ);
	const I32 maxFrameJ = MinMax((const I32)(numTotalFramesJ * params.m_maxPhase), 0, numTotalFramesJ);

	static const I32 sc_maxNumJointDiffWeights = 64;
	JointDiffWeight arrJointDiffWeights[sc_maxNumJointDiffWeights];
	I32 numJointDiffWeights = 0;
	float rootDeltaDiffWeight = 0.0f;
	ConvertJointWeights(params.m_jointDiffDefId,
						skelId,
						&rootDeltaDiffWeight,
						arrJointDiffWeights,
						&numJointDiffWeights,
						sc_maxNumJointDiffWeights);

	if (numJointDiffWeights == 0)
		return false;

	const ArtItemSkeleton* pArtSkeleton = ResourceTable::LookupSkel(skelId).ToArtItem();
	const I16 rootIndex = FindJoint(pArtSkeleton->m_pJointDescs, pArtSkeleton->m_numTotalJoints, SID("root"));

	ScopedTempAllocator jj(FILE_LINE_FUNC);
	ANIM_ASSERT(numTotalFramesJ >= 2);

	const bool bLoopingI = pAnimI->m_flags & ArtItemAnim::kLooping;
	const bool bLoopingJ = pAnimJ->m_flags & ArtItemAnim::kLooping;

	FrameDiffHelper* aJointDifferences = NDI_NEW FrameDiffHelper[numTotalFramesJ];
	for (U32F jFrame = 0; jFrame < numTotalFramesJ; jFrame++)
	{
		aJointDifferences[jFrame].m_frame = jFrame;
		aJointDifferences[jFrame].m_diff = kLargestFloat;
	}

	for (U32F jFrame = minFrameJ; jFrame < maxFrameJ; jFrame++)
	{
		aJointDifferences[jFrame].m_diff = ComputeJointDifference(params.m_precacheAnimBufferI,
																  params.m_precacheAnimBufferJ,
																  params.m_groundApRef,
																  arrJointDiffWeights,
																  numJointDiffWeights,
																  rootIndex,
																  rootDeltaDiffWeight,
																  skelId,
																  pAnimI,
																  frameI,
																  pAnimJ,
																  jFrame);
	}

	if (FALSE_IN_FINAL_BUILD(debugDraw))
	{

		// find out the lowest/highest diff.
		float lowestDiff = kLargestFloat;
		float highestDiff = -kLargestFloat;
		I32 lowestFrame = -1;

		for (U32F jFrame = minFrameJ; jFrame < maxFrameJ; jFrame++)
		{
			float diff = aJointDifferences[jFrame].m_diff;

			if (diff < lowestDiff)
			{
				lowestDiff = diff;
				lowestFrame = jFrame;
			}

			if (diff > highestDiff)
				highestDiff = diff;
		}

		g_prim.Draw(DebugLine2D(Diff2ScreenCoord(0, numTotalFramesJ, 0.0f, lowestDiff, highestDiff), Diff2ScreenCoord(numTotalFramesJ, numTotalFramesJ, 0.0f, lowestDiff, highestDiff), kDebug2DNormalizedCoords, kColorBlue));	// x axis
		g_prim.Draw(DebugLine2D(Diff2ScreenCoord(0, numTotalFramesJ, 0.0f, lowestDiff, highestDiff), Diff2ScreenCoord(0, numTotalFramesJ, 10000000.f, lowestDiff, highestDiff), kDebug2DNormalizedCoords, kColorBlue));	// y axix
		g_prim.Draw(DebugString2D(Diff2ScreenCoord(0, numTotalFramesJ, lowestDiff, lowestDiff, highestDiff) + Vec2(-0.05f, -0.04f), kDebug2DNormalizedCoords, StringBuilder<64>("L:%0.2f", lowestDiff).c_str(), kColorGreen, 0.9f));
		g_prim.Draw(DebugString2D(Diff2ScreenCoord(0, numTotalFramesJ, highestDiff, lowestDiff, highestDiff) + Vec2(-0.05f, 0.02f), kDebug2DNormalizedCoords, StringBuilder<64>("H:%0.2f", highestDiff).c_str(), kColorGreen, 0.9f));

		I32 numFramesToShowText = numTotalFramesJ / 10;
		if (numFramesToShowText < 1) numFramesToShowText = 1;

		Vec2 p2_last = Diff2ScreenCoord(minFrameJ, numTotalFramesJ, aJointDifferences[minFrameJ].m_diff, lowestDiff, highestDiff);
		for (U32F jFrame = minFrameJ + 1; jFrame < maxFrameJ; jFrame++)
		{
			float diff = aJointDifferences[jFrame].m_diff;
			if (diff < kLargestFloat)
			{
				Vec2 p2_1 = Diff2ScreenCoord(jFrame, numTotalFramesJ, diff, lowestDiff, highestDiff);

				g_prim.Draw(DebugLine2D(p2_last, p2_1, kDebug2DNormalizedCoords, kColorYellow, 1.5f));
				if (jFrame % numFramesToShowText == 0)
				{
					Vec2 p2_text = Diff2ScreenCoord(jFrame, numTotalFramesJ, 10000000.f, lowestDiff, highestDiff);
					g_prim.Draw(DebugString2D(p2_text, kDebug2DNormalizedCoords, StringBuilder<64>("%d", jFrame).c_str(), kColorYellow, 0.8f));
				}
				if (jFrame == lowestFrame)
				{
					g_prim.Draw(DebugString2D(p2_1, kDebug2DNormalizedCoords, StringBuilder<64>("L:%d", jFrame).c_str(), kColorGreen, 0.8f));
				}
				p2_last = p2_1;
			}
		}
	}

	// finally, need a sort!
	QuickSort(aJointDifferences, numTotalFramesJ, FrameDiffHelper::Compare);

	if (aJointDifferences[0].m_diff < kLargestFloat)
	{
		*outFrameJ = aJointDifferences[0].m_frame;
		return true;
	}

	return false;
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool ComputeJointDiffTransitionFrame(const JointDiffFuncParams& params, I32* outFrameJ)
{
	const SkeletonId skelId = params.m_skelId;
	const ArtItemAnim* pAnimI = params.m_pAnimI;
	const ArtItemAnim* pAnimJ = params.m_pAnimJ;
	const bool debugDraw = params.m_debugDraw;

	if (params.m_useLookupTable && Memory::IsDebugMemoryAvailable())
	{
		PostMatchLookupResult result = LookupPreProcessTransitionFrame(params.m_skelId,
																	   params.m_jointDiffDefId,
																	   params.m_pAnimI->GetNameId(),
																	   params.m_pAnimJ->GetNameId(),
																	   params.m_frameI,
																	   outFrameJ);
		if (result == kPoseMatchLookupSuccess)
		{
			return true;
		}
		else if (result == kPoseMatchLookupFailed)
		{
			if (params.m_printError)
				MsgAnimWarn("force calculate pose-match in real time! Add (make-pose-matching-entry '%s => '%s) into pose-matching.dc\n", DevKitOnly_StringIdToString(params.m_pAnimI->GetNameId()), DevKitOnly_StringIdToString(params.m_pAnimJ->GetNameId()));
			return false;
		}
		else if (result == kPoseMatchLookupTableNotBuilt)
		{
			// do nothing here now.
			return false;
		}
	}
		
	return ComputeJointDiffFrameInternal(params, nullptr, nullptr, nullptr, nullptr, outFrameJ);
}
