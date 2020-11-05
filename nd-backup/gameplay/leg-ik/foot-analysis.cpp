/*
 * Copyright (c) 2015 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/leg-ik/foot-analysis.h"

#include "corelib/containers/list-array.h"
#include "corelib/math/intersection.h"
#include "corelib/memory/scoped-temp-allocator.h"
#include "corelib/util/bigsort.h"

#include "ndlib/anim/anim-channel.h"
#include "ndlib/anim/anim-control.h"
#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/anim-state-instance.h"
#include "ndlib/anim/anim-state-layer.h"
#include "ndlib/anim/debug-anim-channels.h"
#include "ndlib/anim/effect-anim-entry-tag.h"
#include "ndlib/anim/effect-group.h"
#include "ndlib/anim/footik.h"
#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/anim/ik/jacobian-ik.h"
#include "ndlib/anim/ik/joint-limits.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"
#include "gamelib/ndphys/collision-cast-interface.h"
#include "gamelib/ndphys/collision-cast.h"
#include "gamelib/ndphys/collision-filter.h"

typedef ListArray<Locator> LocatorList;
typedef ListArray<Point> PointList;
typedef ListArray<float> FloatList;
typedef FootAnalysis::FootBase FootBase;
typedef FootAnalysis::FootPlantPosition FootPlantPosition;
typedef FootAnalysis::FootPlantPositions FootPlantPositions;
typedef FootAnalysis::IRootBaseSmoother IRootBaseSmoother;

bool FootAnalysis::s_debugDraw = false;

static CONST_EXPR StringId64 kFootbaseChannels[] = { SID("lFootBase"), SID("rFootBase") };

struct FootPlantTimes
{
	float startTime;
	float endTime;
	float stanceTime;
};

static int Compare(const FootPlantTimes& a, const FootPlantTimes& b)
{
	return a.startTime - b.startTime;
}

typedef ListArray<FootPlantTimes> FootPlantList;

static bool AnimHasJoint(const ArtItemAnim* pAnim, I32 jointIndex)
{
	if (pAnim->m_flags & ArtItemAnim::kAdditive) return false;

	ASSERT(jointIndex < 128);
	const ndanim::ValidBits* pValidBits = ndanim::GetValidBitsArray(pAnim->m_pClipData, 0);
	return pValidBits->IsBitSet(jointIndex);
}

static FootBase GetFootBaseFromAnklePos(SkeletonId skelId,
										const Locator& ankleLoc,
										StringId64 ankleJoint,
										StringId64 heelJoint,
										StringId64 ballJoint,
										Vector_arg groundNormal)
{
	const ArtItemSkeleton* pArtSkeleton = ResourceTable::LookupSkel(skelId).ToArtItem();

	I32F ankleIndex = FindJoint(pArtSkeleton->m_pJointDescs, pArtSkeleton->m_numGameplayJoints, ankleJoint);
	I32F heelJointIndex = FindJoint(pArtSkeleton->m_pJointDescs, pArtSkeleton->m_numGameplayJoints, heelJoint);
	I32F ballJointIndex = FindJoint(pArtSkeleton->m_pJointDescs, pArtSkeleton->m_numGameplayJoints, ballJoint);


	const ndanim::JointHierarchy* pJointHier = pArtSkeleton->m_pAnimHierarchy;
	const ndanim::JointParams* pDefaultParamsLs = ndanim::GetDefaultJointPoseTable(pJointHier);
	const Mat34* pInvBindPose = GetInverseBindPoseTable(pJointHier);
	const Mat34& invBPAnkle = pInvBindPose[ankleIndex];
	const Mat34& invBPHeelJoint = pInvBindPose[heelJointIndex];
	const Mat34& invBPBallJoint = pInvBindPose[ballJointIndex];

	//The footbase is a line at the lowest point on the foot that is the length of the foot
	Point heelJointProjToGroundBindPose = Point(MulPointMatrix(Point(kOrigin).GetVec4(), Inverse(invBPHeelJoint.GetMat44())));
	heelJointProjToGroundBindPose.SetY(0.0f);

	Point ballJointProjToGroundBindPose = Point(MulPointMatrix(Point(kOrigin).GetVec4(), Inverse(invBPBallJoint.GetMat44())));
	ballJointProjToGroundBindPose.SetY(0.0f);

	Point hellJointBaseAnkleSpace = Point(MulPointMatrix(heelJointProjToGroundBindPose.GetVec4(), invBPAnkle.GetMat44()));
	Point ballJointBaseAnkleSpace = Point(MulPointMatrix(ballJointProjToGroundBindPose.GetVec4(), invBPAnkle.GetMat44()));
	Vector upAnkleSpace = Vector(MulVec4Matrix(Vector(kUnitYAxis).GetVec4(), invBPAnkle.GetMat44()));

	Point heelJointBaseWS = ankleLoc.TransformPoint(hellJointBaseAnkleSpace);
	Point ballJointBaseWS = ankleLoc.TransformPoint(ballJointBaseAnkleSpace);

	Point anklePosBindPose = Point(MulPointMatrix(Point(kOrigin).GetVec4(), Inverse(invBPAnkle.GetMat44())));

// 	g_prim.Draw(DebugCross(anklePosBindPose, 0.03f, kColorWhite), kPrimDuration1FramePauseable);
// 	g_prim.Draw(DebugCross(heelJointProjToGroundBindPose, 0.03f, kColorBlue), kPrimDuration1FramePauseable);
// 	g_prim.Draw(DebugCross(ballJointProjToGroundBindPose, 0.03f, kColorRed), kPrimDuration1FramePauseable);
//
// 	g_prim.Draw(DebugCross(ankleLoc.GetTranslation(), 0.03f, kColorWhite), kPrimDuration1FramePauseable);
// 	g_prim.Draw(DebugCross(heelJointBaseWS, 0.03f, kColorBlue), kPrimDuration1FramePauseable);
// 	g_prim.Draw(DebugCross(ballJointBaseWS, 0.03f, kColorRed), kPrimDuration1FramePauseable);


	float heelDist = Dot(AsVector(heelJointBaseWS), groundNormal);
	float ballDist = Dot(AsVector(ballJointBaseWS), groundNormal);

	float footLength = Dist(heelJointBaseWS, ballJointBaseWS);

	Vector footDirectionWs = (ballJointBaseWS - heelJointBaseWS);
	Vector ankleUpWS = ankleLoc.TransformVector(upAnkleSpace);
	Vector footDirection = footDirectionWs - groundNormal*Dot(groundNormal, footDirectionWs);

	float dotNormalUp = Dot(groundNormal, ankleUpWS);
	float test = Dot(ankleUpWS, footDirectionWs);
	float footAngle = RadiansToDegrees(Atan2(Dot(groundNormal, Normalize(footDirectionWs)), dotNormalUp));

	float dirBlend = LerpScaleClamp(-90.0f, -85.0, 1.0f, 0.0f, footAngle);
	footDirection = Slerp(footDirection, ankleUpWS, dirBlend);
	footDirection -=  groundNormal*Dot(groundNormal, footDirection);
	footDirection = SafeNormalize(footDirection, kUnitYAxis)*footLength;

	//float alpha = 20.0f;
	//float balance = Atan((heelDist - ballDist)/footLength*alpha)/PI + 0.5f;

	float alpha = 20.0f;
	float balance = Atan(-(heelDist - ballDist)/footLength*alpha)/PI+0.5f;

	Point footBase = AsPoint(AsVector(heelJointBaseWS)*balance +  (AsVector(ballJointBaseWS) - footDirection)*(1.0f-balance));

	//g_prim.Draw(DebugArrow(footBase, footDirection, kColorGreen), kPrimDuration1FramePauseable);

	FootBase result;
	result.loc.SetTranslation(footBase);
	result.loc.SetRotation(QuatFromLookAt(Normalize(footDirection), groundNormal));
	result.footLength = footLength;

	//g_prim.Draw(DebugCoordAxesLabeled(result.loc, "Foot Base", result.footLength), kPrimDuration1FramePauseable);

	return result;
}

static bool GetFootBaseFromAnimChannel(const ArtItemAnim* pAnim, const StringId64 channelId, const float phase, const bool mirror, FootBase& outFootBase)
{
	ndanim::JointParams params;
	bool valid = EvaluateChannelInAnim(pAnim->m_skelID, pAnim, channelId, phase, &params, mirror, true);
	if (valid)
	{
		outFootBase.loc.SetTranslation(params.m_trans);
		outFootBase.loc.SetRotation(params.m_quat);
		outFootBase.footLength = params.m_scale.Z();
		return true;
	}
	return false;
}

#if ENABLE_DEBUG_ANIM_CHANNELS
class FootBaseChannelBuild : public IChannelSampleBuilder
{
public:
	FootBaseChannelBuild(StringId64 ankleJoint, StringId64 heelJoint, StringId64 ballJoint)
		: m_ankleJoint(ankleJoint)
		, m_heelJoint(heelJoint)
		, m_ballJoint(ballJoint)
	{

	}
	virtual void BuildFrame(const ArtItemAnim* pAnim, float sample, AnimChannelFormat* pResult) const override
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

		ALWAYS_ASSERT(valid);
		if (valid)
		{
			I32F ankleIndex = FindJoint(pArtSkeleton->m_pJointDescs, pArtSkeleton->m_numGameplayJoints, m_ankleJoint);
			ALWAYS_ASSERT(ankleIndex >= 0);

			Locator jointLocOS(aJointTransforms[ankleIndex]);

			Vector groundNormal(kUnitYAxis);

			Locator groundReference(kIdentity);
			if (EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("apReference-ground"), pAnim->m_pClipData->m_fNumFrameIntervals > 0.0f ? sample/pAnim->m_pClipData->m_fNumFrameIntervals : 0.0f, &groundReference))
			{
				groundNormal = GetLocalY(groundReference);
			}


			FootBase footbase = GetFootBaseFromAnklePos(pAnim->m_skelID, jointLocOS, m_ankleJoint, m_heelJoint, m_ballJoint, groundNormal);

			pResult->m_trans = footbase.loc.Pos();
			pResult->m_quat = footbase.loc.Rot();
			pResult->m_scale = Vec3(1.0f, 1.0f, footbase.footLength);
		}
	}

private:
	StringId64 m_ankleJoint;
	StringId64 m_heelJoint;
	StringId64 m_ballJoint;
};
#endif

static bool AddFootChannelsToAnim(const ArtItemAnim* pAnim, FootIkCharacterType charType)
{
#if ENABLE_DEBUG_ANIM_CHANNELS
	SkeletonId skelId = pAnim->m_skelID;
	const ArtItemSkeleton* pArtSkeleton = ResourceTable::LookupSkel(skelId).ToArtItem();

	const int legCount = GetLegCountForCharacterType(charType);
	const int pairCount = legCount / kLegCount;

	for (int iPair = 0; iPair < pairCount; ++iPair)
	{
		const int iLeftLeg = iPair * kLegCount;
		const int iRightLeg = iLeftLeg + 1;

		I32F rAnkleIndex = FindJoint(pArtSkeleton->m_pJointDescs, pArtSkeleton->m_numGameplayJoints, GetJointName(charType, JointFromLegAndJointType(iRightLeg, kJointTypeAnkle)));
		I32F lAnkleIndex = FindJoint(pArtSkeleton->m_pJointDescs, pArtSkeleton->m_numGameplayJoints, GetJointName(charType, JointFromLegAndJointType(iLeftLeg, kJointTypeAnkle)));

		if (AnimHasJoint(pAnim, rAnkleIndex) && AnimHasJoint(pAnim, lAnkleIndex))
		{
			AtomicLockJanitor jj(DebugAnimChannels::Get()->GetLock(), FILE_LINE_FUNC);
			const StringId64 rightAnkle = GetAnkleChannelName(charType, iRightLeg);
			const StringId64 leftAnkle = GetAnkleChannelName(charType, iLeftLeg);
			if (FindChannel(pAnim, rightAnkle) == nullptr)
			{
				JointChannelBuild rightAnkleBuild(rAnkleIndex);
				const CompressedChannel* pRightAnkleChannel = DebugChannelBuilder().Build(pAnim, &rightAnkleBuild);
				DebugAnimChannels::Get()->AddChannel(pAnim, rightAnkle, pRightAnkleChannel);
			}
			if (FindChannel(pAnim, leftAnkle) == nullptr)
			{
				JointChannelBuild leftAnkleBuild(lAnkleIndex);
				const CompressedChannel* pLeftAnkleChannel = DebugChannelBuilder().Build(pAnim, &leftAnkleBuild);
				DebugAnimChannels::Get()->AddChannel(pAnim, leftAnkle, pLeftAnkleChannel);
			}

			// TODO@QUAD do we need to do something different for the front legs and footbase channels?
			if (FindChannel(pAnim, kFootbaseChannels[0]) == nullptr)
			{
				FootBaseChannelBuild footbaseBuild(SID("l_ankle"), SID("l_heel"), SID("l_ball"));
				const CompressedChannel* pFootBaseChannel = DebugChannelBuilder().Build(pAnim, &footbaseBuild);
				DebugAnimChannels::Get()->AddChannel(pAnim, kFootbaseChannels[0], pFootBaseChannel);
			}
			if (FindChannel(pAnim, kFootbaseChannels[1]) == nullptr)
			{
				FootBaseChannelBuild footbaseBuild(SID("r_ankle"), SID("r_heel"), SID("r_ball"));
				const CompressedChannel* pFootBaseChannel = DebugChannelBuilder().Build(pAnim, &footbaseBuild);
				DebugAnimChannels::Get()->AddChannel(pAnim, kFootbaseChannels[1], pFootBaseChannel);
			}
			return true;
		}
	}
#endif
	return false;
}

static void ExtractChannel(const ArtItemAnim* pAnim, LocatorList& list, StringId64 channelId)
{
	int numFrames = pAnim->m_pClipData->m_numTotalFrames;

	for (int i = 0; i < numFrames; i++)
	{
		float phase = float(i)/Max(numFrames - 1, 1);
		Locator channelLoc;
		bool channelEval = EvaluateChannelInAnim( pAnim->m_skelID,  pAnim, channelId, phase, &channelLoc);
		Locator alignLoc;
		bool alignEval = EvaluateChannelInAnim( pAnim->m_skelID,  pAnim, SID("align"), phase, &alignLoc);

		ASSERT(channelEval && alignEval);
		list.PushBack(alignLoc.TransformLocator(channelLoc));
	}
}

static void GetJointFromAnkle(SkeletonId skelId,
							  const LocatorList& ankleLocs,
							  StringId64 ankleJoint,
							  StringId64 otherJoint,
							  PointList& jointPos)
{
	const ArtItemSkeleton* pArtSkeleton = ResourceTable::LookupSkel(skelId).ToArtItem();

	I32F ankleIndex = FindJoint(pArtSkeleton->m_pJointDescs, pArtSkeleton->m_numGameplayJoints, ankleJoint);
	I32F jointIndex = FindJoint(pArtSkeleton->m_pJointDescs, pArtSkeleton->m_numGameplayJoints, otherJoint);

	const ndanim::JointHierarchy* pJointHier = pArtSkeleton->m_pAnimHierarchy;
	const ndanim::JointParams* pDefaultParamsLs = ndanim::GetDefaultJointPoseTable(pJointHier);
	const Mat34* pInvBindPose = GetInverseBindPoseTable(pJointHier);
	const Mat34& invBPAnkle = pInvBindPose[ankleIndex];
	const Mat34& invBPJoint = pInvBindPose[jointIndex];

	Mat44 jointInAnkleSpaceM = Inverse(invBPJoint.GetMat44())*invBPAnkle.GetMat44();

	for (int i = 0; i < ankleLocs.Size(); i++)
	{
		Point anklePos = Point(MulPointMatrix(Point(kOrigin).GetVec4(), (jointInAnkleSpaceM*ankleLocs[i].AsTransform().GetMat44())));
		jointPos.PushBack(anklePos);
	}
}

static void GetGroundSpeedFromPositions(const PointList& positions,
										Plane groundPlane,
										float deltaTime,
										FloatList& speedList)
{
	for (int i = 1; i < positions.Size(); i++)
	{
		Point projected0 = groundPlane.ProjectPoint(positions[i]);
		Point projected1 = groundPlane.ProjectPoint(positions[i-1]);

		float speed = Dist(projected1, projected0)/deltaTime;
		speedList.PushBack(speed);
	}
}

static void GetEFFFootPlantsForAnim(const ArtItemAnim* pAnim, StringId64 jointId, FootPlantList& plants)
{
	if (const EffectAnim* pEffectAnim = pAnim->m_pEffectAnim)
	{
		for (int i = 0; i < pEffectAnim->m_numEffects; i++)
		{
			const EffectAnimEntry* pEffect = &pEffectAnim->m_pEffects[i];
			if (pEffect->GetNameId() == SID("foot-plant"))
			{
				if (const EffectAnimEntryTag* pTag = pEffect->GetTagByName(SID("joint")))
				{
					if (pTag->GetValueAsStringId() ==jointId)
					{
						FootPlantTimes plant;
						plant.startTime = pEffect->GetFrame();
						plant.endTime = pEffect->HasEndFrame() ? pEffect->GetEndFrame() : pEffect->GetFrame();
						plant.stanceTime = Lerp(plant.startTime, plant.endTime, 0.5f);
						plants.PushBack(plant);

						if (pAnim->m_flags & ArtItemAnim::kLooping)
						{
							int numFrames = pAnim->m_pClipData->m_numTotalFrames;
							{
								FootPlantTimes prevPlant = plant;
								prevPlant.startTime -= numFrames;
								prevPlant.endTime -= numFrames;
								prevPlant.stanceTime -= numFrames;
								plants.PushBack(prevPlant);
							}

							{
								FootPlantTimes prevPlant = plant;
								prevPlant.startTime -= numFrames*2;
								prevPlant.endTime -= numFrames*2;
								prevPlant.stanceTime -= numFrames*2;
								plants.PushBack(prevPlant);
							}


							FootPlantTimes nexPlant = plant;
							nexPlant.startTime += numFrames;
							nexPlant.endTime += numFrames;
							nexPlant.stanceTime += numFrames;
							plants.PushBack(nexPlant);

							FootPlantTimes nexNextPlant = plant;
							nexNextPlant.startTime += numFrames*2;
							nexNextPlant.endTime += numFrames*2;
							nexNextPlant.stanceTime += numFrames*2;
							plants.PushBack(nexNextPlant);

						}
					}
				}
			}
		}
	}

	if (plants.Size() > 1)
	{
		QuickSort(plants.ArrayPointer(), plants.Size(), Compare);
	}
}

struct FootPlantRange
{
	FootPlantRange()
		: pLower(nullptr)
		, pUpper(nullptr)
	{}

	const FootPlantTimes* pLower;
	const FootPlantTimes* pUpper;
};

static FootPlantRange GetFootPlantFrame(const FootPlantList& plants, float frame)
{
	const FootPlantTimes* pLower = nullptr;
	const FootPlantTimes* pUpper = nullptr;

	for (int i = 0; i < plants.Size(); i++)
	{
		const FootPlantTimes& currentPlant = plants[i];

		float midTime = Lerp(currentPlant.endTime, currentPlant.startTime, 0.5f);
		if (frame >= midTime)
		{
			if (!pLower || Compare(*pLower, currentPlant) < 0)
			{
				pLower = &currentPlant;
			}
		}
		else if (frame < midTime)
		{
			if (!pUpper || Compare(*pUpper, currentPlant) > 0)
			{
				pUpper = &currentPlant;
			}
		}
	}

	FootPlantRange result;
	result.pLower = pLower;
	result.pUpper = pUpper;

	return result;
}

static float ComputeSwingPhaseFromFootPlants(const FootPlantList& plants, float maxTime, float frame)
{
	FootPlantRange plantRange = GetFootPlantFrame(plants, frame);

	float defaultStartTime = 0.0f;
	if (plantRange.pUpper && plantRange.pUpper->startTime <= frame)
	{
		defaultStartTime = plantRange.pUpper->stanceTime;
	}
	float startTime = plantRange.pLower ? plantRange.pLower->stanceTime : defaultStartTime;
	float endTime = plantRange.pUpper ? plantRange.pUpper->stanceTime : maxTime;

	return LerpScaleClamp(startTime, endTime, 0.0f, 1.0f, frame);
}

static float ComputeFlightPhaseFromFootPlants(const FootPlantList& plants, float maxTime, float frame)
{
	FootPlantRange plantRange = GetFootPlantFrame(plants, frame);

	//Check if we are in a plant interval
	if (plantRange.pLower && plantRange.pLower->startTime >= frame && plantRange.pLower->endTime <= frame)
	{
		return 0.0f;
	}
	if (plantRange.pUpper && plantRange.pUpper->startTime >= frame && plantRange.pUpper->endTime <= frame)
	{
		return 0.0f;
	}

	float startTime = plantRange.pLower ? plantRange.pLower->endTime : 0.0f;
	float endTime = plantRange.pUpper ? plantRange.pUpper->startTime : maxTime;

	return LerpScaleClamp(startTime, endTime, 0.0f, 1.0f, frame);
}

template <typename T> T Min(const ListArray<T>& list)
{
	T min = list[0];
	for (int i = 1; i < list.Size(); i++)
	{
		if (list[i] < min)
		{
			min = list[i];
		}
	}
	return min;
}

template <typename T> T Max(const ListArray<T>& list)
{
	T max = list[0];
	for (int i = 1; i < list.Size(); i++)
	{
		if (list[i] > max)
		{
			max = list[i];
		}
	}
	return max;
}

static float DistToGround(Point_arg p, const Plane& groundPlane)
{
	Point planePt;
	LinePlaneIntersect(groundPlane.ProjectPoint(p), groundPlane.GetNormal(), p, p-kUnitYAxis, nullptr, &planePt);
	return p.Y() - planePt.Y();
}

static void DrawPlants(PrimServerWrapper2D& ps2d, const FootPlantList& rightPlants, Color color)
{
	for (int i = 0; i < rightPlants.Size(); i++)
	{
		ps2d.DrawQuad(
			Vec2(rightPlants[i].startTime, 1), Vec2(rightPlants[i].endTime, 1),
			Vec2(rightPlants[i].endTime, 0), Vec2(rightPlants[i].startTime, 0),
			color);
	}
}

static float GroundHeightWeightFromSwingPhase(float swingPhase)
{
	float epsilon = 0.01f;
	return (Cos(2.0f*PI*swingPhase)+1.f)/2.f + epsilon;
}

static void DrawSwingPhase(PrimServerWrapper2D& ps2d, const FootPlantList& rightPlants, int numFrames, Color color)
{
	int samples = 1;

	for (int i = 1; i <= numFrames*samples; i++)
	{
		float t0 = float(i)/samples;
		float t1 = float(i-1)/samples;
		float swingPhase0 = ComputeSwingPhaseFromFootPlants(rightPlants, numFrames, t0);
		float swingPhase1 = ComputeSwingPhaseFromFootPlants(rightPlants, numFrames, t1);

		swingPhase0 = GroundHeightWeightFromSwingPhase(swingPhase0);
		swingPhase1 = GroundHeightWeightFromSwingPhase(swingPhase1);

		ps2d.DrawLine(Vec2(t0, swingPhase0), Vec2(t1, swingPhase1), color);

		float flightPhase0 = ComputeFlightPhaseFromFootPlants(rightPlants, numFrames, t0);
		float flightPhase1 = ComputeFlightPhaseFromFootPlants(rightPlants, numFrames, t1);

		ps2d.DrawLine(Vec2(t0, flightPhase0), Vec2(t1, flightPhase1), color);

	}
}

static void DrawGroundHeightWeight(PrimServerWrapper2D& ps2d,
								   const FootPlantList& rightPlants,
								   const FootPlantList& leftPlants,
								   int numFrames,
								   Color color)
{
	int samples = 1;

	for (int i = 1; i < numFrames*samples; i++)
	{
		float t0 = float(i)/samples;
		float t1 = float(i-1)/samples;
		float swingPhaseR0 = ComputeSwingPhaseFromFootPlants(rightPlants, numFrames, t0);
		float swingPhaseR1 = ComputeSwingPhaseFromFootPlants(rightPlants, numFrames, t1);
		float weightR0 = GroundHeightWeightFromSwingPhase(swingPhaseR0);
		float weightR1 = GroundHeightWeightFromSwingPhase(swingPhaseR1);

		float swingPhaseL0 = ComputeSwingPhaseFromFootPlants(leftPlants, numFrames, t0);
		float swingPhaseL1 = ComputeSwingPhaseFromFootPlants(leftPlants, numFrames, t1);
		float weightL0 = GroundHeightWeightFromSwingPhase(swingPhaseL0);
		float weightL1 = GroundHeightWeightFromSwingPhase(swingPhaseL1);

		float finalWeight0 = (weightR0)/(weightR0+weightL0);
		float finalWeight1 = (weightR1)/(weightR1+weightL1);


		ps2d.DrawLine(Vec2(t0, finalWeight0), Vec2(t1, finalWeight1), color);

	}
}

void FootAnalysis::AnalyzeFeet(const ArtItemAnim* pAnim, float phase, FootIkCharacterType charType)
{
	if (!AddFootChannelsToAnim(pAnim, charType))
	{
		return;
	}

	ScopedTempAllocator aloc(FILE_LINE_FUNC);

	int numFrames = pAnim->m_pClipData->m_numTotalFrames;

	if (numFrames < 2)
	{
		return;
	}

	LocatorList rightAnkles(numFrames);
	PointList rightHeelPos(numFrames);
	PointList rightBallPos(numFrames);
	PointList rightFootBasePos(numFrames);
	FloatList heelSpeeds(numFrames - 1);
	FloatList ballSpeeds(numFrames - 1);
	FloatList baseSpeeds(numFrames - 1);
	FootPlantList rightPlants(50);
	FootPlantList leftPlants(50);
	Plane groundPlane(kOrigin, kUnitYAxis);

	// TODO@QUAD we could analyze both feet in the pair. Right now it seems we only analyze the right leg?
	const StringId64 rightAnkleChannelName = GetAnkleChannelName(charType, kRightLeg);
	ExtractChannel(pAnim, rightAnkles, rightAnkleChannelName);

	const StringId64 rightAnkleJointName = GetJointName(charType, kFootIkJointRightAnkle);
	const StringId64 rightHeelJointName = GetJointName(charType, kFootIkJointRightHeel);
	const StringId64 rightBallJointName = GetJointName(charType, kFootIkJointRightBall);
	const StringId64 leftBallJointName = GetJointName(charType, kFootIkJointLeftBall);
	GetJointFromAnkle(pAnim->m_skelID, rightAnkles, rightAnkleJointName, rightHeelJointName, rightHeelPos);
	GetJointFromAnkle(pAnim->m_skelID, rightAnkles, rightAnkleJointName, rightBallJointName, rightBallPos);
	//ExtractChannel(pAnim, rightAnkles, SID("rAnkle"));
	//GetJointFromAnkle(pAnim->m_skelID, rightAnkles, SID("r_ankle"), SID("r_heel"), rightHeelPos);
	//GetJointFromAnkle(pAnim->m_skelID, rightAnkles, SID("r_ankle"), SID("r_ball"), rightBallPos);
	GetGroundSpeedFromPositions(rightHeelPos, groundPlane, pAnim->m_pClipData->m_secondsPerFrame, heelSpeeds);
	GetGroundSpeedFromPositions(rightBallPos, groundPlane, pAnim->m_pClipData->m_secondsPerFrame, ballSpeeds);
	GetEFFFootPlantsForAnim(pAnim, rightBallJointName, rightPlants);
	GetEFFFootPlantsForAnim(pAnim, leftBallJointName, leftPlants);
	//GetEFFFootPlantsForAnim(pAnim, SID("r_ball"), rightPlants);
	//GetEFFFootPlantsForAnim(pAnim, SID("l_ball"), leftPlants);

	if (false)
	{

		for (int i = 1; i < rightAnkles.Size(); i++)
		{
			g_prim.Draw(DebugLine(rightAnkles[i-1].GetTranslation(), rightAnkles[i].GetTranslation(), kColorMagenta), kPrimDuration1FramePauseable);
		}

		for (int i = 1; i < rightHeelPos.Size(); i++)
		{
			g_prim.Draw(DebugLine(rightHeelPos[i-1], rightHeelPos[i], kColorRed), kPrimDuration1FramePauseable);
		}

		for (int i = 1; i < rightBallPos.Size(); i++)
		{
			g_prim.Draw(DebugLine(rightBallPos[i-1], rightBallPos[i], kColorGreen), kPrimDuration1FramePauseable);
		}

		for (int i = 0; i < rightHeelPos.Size(); i++)
		{
			const Point& heelPos = rightHeelPos[i];
			const Point& ballPos = rightBallPos[i];

			float heelDist = DistToGround(heelPos, groundPlane);
			float ballDist = DistToGround(ballPos, groundPlane);

			float footLength = Dist(heelPos, ballPos);

			//float alpha = 20.0f;
			//float balance = Atan((heelDist - ballDist)/footLength*alpha)/PI + 0.5f;



			Point basePos = heelPos;
			Vector baseDir = ballPos - heelPos;
			if (ballDist < heelDist)
			{
				basePos = ballPos;
				baseDir = -baseDir;
			}
			Vector baseDirFlat = baseDir - groundPlane.GetNormal()*Dot(groundPlane.GetNormal(), baseDir);

			Point midFootBase = basePos + SafeNormalize(baseDirFlat, kUnitZAxis)*footLength/2.0f;

			rightFootBasePos.PushBack(midFootBase);

			g_prim.Draw(DebugLine(basePos, SafeNormalize(baseDirFlat, kUnitZAxis)*footLength, kColorOrange), kPrimDuration1FramePauseable);
		}
	}

	GetGroundSpeedFromPositions(rightFootBasePos, groundPlane, pAnim->m_pClipData->m_secondsPerFrame, baseSpeeds);


	const Vec2 drawPos = Vec2(50.0f, 500.0f);
	const float drawSizeX = 1200.0f;
	const float drawSizeY = 300.0f;
	const float minSpeed = Min(heelSpeeds);
	const float maxSpeed =  Max(heelSpeeds);
	PrimServerWrapper2D ps2d(MakeVirtualScreenTransform(Aabb(Point(0, 0, 0),	Point(heelSpeeds.Size(), 1, 1.0f)),
		Aabb(Point(drawPos.x, drawPos.y, 0.0f),			Point(drawPos.x + drawSizeX, drawPos.y - drawSizeY, 1.0f))));

	ps2d.DrawLine(Vec2(phase*numFrames, -0.5f),Vec2(phase*numFrames, 1.5f), kColorBlack);

// 	for (int i = 1; i < heelSpeeds.Size(); i++)
// 	{
// 		ps2d.DrawLine(Vec2(i, heelSpeeds[i]), Vec2(i-1, heelSpeeds[i-1]), kColorRed);
// 	}
// 	for (int i = 1; i < ballSpeeds.Size(); i++)
// 	{
// 		ps2d.DrawLine(Vec2(i, ballSpeeds[i]), Vec2(i-1, ballSpeeds[i-1]), kColorGreen);
// 	}
// 	for (int i = 1; i < ballSpeeds.Size(); i++)
// 	{
// 		ps2d.DrawLine(Vec2(i, baseSpeeds[i]), Vec2(i-1, baseSpeeds[i-1]), kColorOrange);
// 	}

	DrawPlants(ps2d, rightPlants, kColorRedTrans);
	DrawPlants(ps2d, leftPlants, kColorBlueTrans);
// 	for (int i = 0; i < rightPlants.Size(); i++)
// 	{
// 		ps2d.DrawQuad(
// 			Vec2(rightPlants[i].startTime, maxSpeed), Vec2(rightPlants[i].endTime, maxSpeed),
// 			Vec2(rightPlants[i].endTime, minSpeed), Vec2(rightPlants[i].startTime, minSpeed),
// 			 kColorBlueTrans);
// 	}

	DrawSwingPhase(ps2d, rightPlants, numFrames, kColorRed);
	DrawSwingPhase(ps2d, leftPlants, numFrames, kColorBlue);
	DrawGroundHeightWeight(ps2d, rightPlants, leftPlants, numFrames, kColorYellow);

// 	int samples = 1;
//
// 	for (int i = 1; i < numFrames*samples; i++)
// 	{
// 		float t0 = float(i)/samples;
// 		float t1 = float(i-1)/samples;
// 		float swingPhase0 = ComputeSwingPhaseFromFootPlants(rightPlants, numFrames, t0);
// 		float swingPhase1 = ComputeSwingPhaseFromFootPlants(rightPlants, numFrames, t1);
//  		float epsilon = 0.01f;

//  		swingPhase0 = (Cos(2*PI*swingPhase0)+1)/2 + epsilon;

//  		swingPhase1 = (Cos(2*PI*swingPhase1)+1)/2 + epsilon;

// 		ps2d.DrawLine(Vec2(t0, swingPhase0), Vec2(t1, swingPhase1), kColorBlack);
//
// 	}

	MsgCon("Min heel speed: %f\n", Min(heelSpeeds));
}

struct LegDeltaLimits
{
	float m_desired;
	float m_max;
	float m_give;
};

static LegDeltaLimits ComputeMaxLegRootDelta(const Locator& alignWs, LegIkChain& legChain, Point_arg desiredAnklePosWs)
{
	const Point anklePos = legChain.GetAnkleLocWs().GetTranslation();
	const Point kneePos = legChain.GetKneeLocWs().GetTranslation();
	const Point hipPos = legChain.GetHipLocWs().GetTranslation();

	const Scalar desiredLength = Dist(anklePos, hipPos);

	//Limit the max angle the ik can move the knee to reduce pops in the knee angle.
	float maxKneeAngle = Lerp(180.0f, Max(RADIANS_TO_DEGREES(legChain.GetKneeAngle()), 0.0f), 0.5f);
	const Scalar legLength = Max(desiredLength,
								 (Dist(anklePos, kneePos) * Cos(DegreesToRadians(180.0f - maxKneeAngle))
								  + Dist(kneePos, hipPos)));

	const Scalar give = legLength - desiredLength;
	const Sphere limitSphereMax(Point(desiredAnklePosWs.X(), anklePos.Y(), desiredAnklePosWs.Z()), legLength);

	const Point sphereBaseWs = hipPos;

	const Point initialStartSeg = PointFromXzAndY(sphereBaseWs, anklePos);
	const Point initialEndSeg   = sphereBaseWs + Vector(0.0f, legLength * 2.0f, 0.0f);

	Point startSeg = initialStartSeg;
	Point endSeg   = initialEndSeg;

	const bool clip = limitSphereMax.ClipLineSegment(startSeg, endSeg);
	//GAMEPLAY_ASSERT(clip);
	const float max = endSeg.Y() - anklePos.Y();

	const Sphere limitSphereDesired(Point(desiredAnklePosWs.X(), anklePos.Y(), desiredAnklePosWs.Z()), desiredLength);

	const Point initialStartDesired = PointFromXzAndY(sphereBaseWs, anklePos);
	const Point initialEndDesired = sphereBaseWs + Vector(0.0f, legLength * 2.0f, 0.0f);

	Point startSegDesired = initialStartDesired;
	Point endSegDesired	  = initialEndDesired;

	const bool clipDesired = limitSphereDesired.ClipLineSegment(startSegDesired, endSegDesired);
	//GAMEPLAY_ASSERT(clipDesired);
	const float desired = endSegDesired.Y() - anklePos.Y();

	if (FALSE_IN_FINAL_BUILD(!clip))
	{
		PrimAttrib attrib;
		//attrib.SetWireframe(true);
		//attrib.SetHiddenLineAlpha(true);
		g_prim.Draw(DebugSphere(limitSphereMax, kColorGrayTrans, attrib), Seconds(5.0f));
		g_prim.Draw(DebugLine(initialStartSeg, initialEndSeg, kColorRed, 3.0f, kPrimEnableHiddenLineAlpha), Seconds(5.0f));
		//g_prim.Draw(DebugLine(startSegDesired, endSegDesired, kColorGrayTrans, 2.0f, kPrimEnableHiddenLineAlpha), Seconds(5.0f));
	}
	if (FALSE_IN_FINAL_BUILD(!clipDesired))
	{
		const float d = DistPointSegment(limitSphereDesired.GetCenter(), initialStartDesired, initialEndDesired);

		PrimAttrib attrib;
		//attrib.SetWireframe(true);
		//attrib.SetHiddenLineAlpha(true);
		g_prim.Draw(DebugSphere(limitSphereDesired, kColorGrayTrans, attrib), Seconds(5.0f));
		g_prim.Draw(DebugLine(initialStartDesired, initialEndDesired, kColorCyan, 3.0f, kPrimEnableHiddenLineAlpha), Seconds(5.0f));
	}

	LegDeltaLimits result;
	result.m_desired = desired;
	result.m_max	 = max;
	result.m_give = give;
	return result;
}

static Maybe<FootPlantPosition> GetFootPlantPosition(const FootPlantTimes& times,
													 StringId64 footChannelId,
													 const ArtItemAnim* pAnim,
													 const bool mirror)
{
	float phase = times.stanceTime/pAnim->m_pClipData->m_numTotalFrames;
	Locator alignSpace(kIdentity);
	if (phase > 1.0f || phase < 0.0f)
	{
		if (!(pAnim->m_flags & ArtItemAnim::kLooping))
		{
			return MAYBE::kNothing;
		}
		ASSERT(pAnim->m_flags & ArtItemAnim::kLooping);

		if (phase < 0.f)
		{
			phase += 1.0f;
			bool alignValid = EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("align"), 1.0f, &alignSpace, mirror);
			alignSpace = Inverse(alignSpace);
		}
		if (phase > 1.0f)
		{
			phase -= 1.0f;
			bool alignValid = EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("align"), 1.0f, &alignSpace, mirror);
		}
	}
	Locator align(kIdentity);
	bool alignValid = EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("align"), phase, &align, mirror);
	FootBase footBase;
	bool footValid = GetFootBaseFromAnimChannel(pAnim, footChannelId, phase, mirror, footBase);

	align = alignSpace.TransformLocator(align);

	if (!(alignValid && footValid))
	{
		return MAYBE::kNothing;
	}
	footBase.loc = align.TransformLocator(footBase.loc);

	Vector groundNormal(kUnitYAxis);
	Locator groundPlaneAp(kIdentity);
	bool groundApValid = EvaluateChannelInAnim(pAnim->m_skelID, pAnim, SID("apReference-ground"), phase, &groundPlaneAp);
	float footHeight = footBase.GetHeelPoint().Y() - align.GetTranslation().Y();
	if (groundApValid)
	{
		Locator groundPlaneWS = align.TransformLocator(groundPlaneAp);
		groundNormal = GetLocalY(groundPlaneAp.GetRotation());
		Point heelProjToGround(footBase.GetHeelPoint());
		LinePlaneIntersect(groundPlaneWS.GetTranslation(), groundNormal, footBase.GetHeelPoint(), footBase.GetHeelPoint() + kUnitYAxis, nullptr, &heelProjToGround);
		Point ballProjToGround(footBase.GetHeelPoint());
		LinePlaneIntersect(groundPlaneWS.GetTranslation(), groundNormal, footBase.GetBallPoint(), footBase.GetBallPoint() + kUnitYAxis, nullptr, &ballProjToGround);
		footHeight = Min(footBase.GetHeelPoint().Y() - heelProjToGround.Y(), footBase.GetBallPoint().Y() - ballProjToGround.Y());
	}

	FootPlantPosition result;
	result.align = align;
	result.groundNormal = groundNormal;
	result.foot = footBase;
	result.footHeightOffGround = footHeight;
	return result;
}

static FootPlantPositions GetFootPlantPositions(const FootPlantList& plants,
												float curFrame,
												StringId64 footChannelId,
												const ArtItemAnim* pAnim,
												const bool mirror)
{
	FootPlantRange plantRange = GetFootPlantFrame(plants, curFrame);
	FootPlantPositions result;
	if (plantRange.pLower)
	{
		result.start = GetFootPlantPosition(*plantRange.pLower, footChannelId, pAnim, mirror);
	}
	if (plantRange.pUpper)
	{
		result.end = GetFootPlantPosition(*plantRange.pUpper, footChannelId, pAnim, mirror);
	}
	return result;
}

static Vector LimitGroundNormal(Vector_arg n, float minY)
{
	if (n.Y() < minY)
	{
		return Vector(0.0f, minY, 0.0f) + SafeNormalize(VectorXz(n), kZero)*(1.0f-minY*minY);
	}
	else
	{
		return n;
	}
}

static FootPlantPosition CastFootPlantPositionToGround(const FootPlantPosition& animPosition)
{
	FootPlantPosition collisionPosition = animPosition;


	RayCastJob ray;
	ray.Open(2,1,ICollCastJob::kCollCastSynchronous);
	ray.SetProbeExtents(0, animPosition.foot.GetHeelPoint() + Vector(kUnitYAxis)*10.0f, animPosition.foot.GetHeelPoint() - Vector(kUnitYAxis)*10.0f);
	ray.SetProbeFilter(0, CollideFilter(Collide::kLayerMaskGeneral));
	ray.SetProbeExtents(1, animPosition.foot.GetBallPoint() + Vector(kUnitYAxis)*10.0f, animPosition.foot.GetBallPoint() - Vector(kUnitYAxis)*10.0f);
	ray.SetProbeFilter(1, CollideFilter(Collide::kLayerMaskGeneral));
	ray.Kick(FILE_LINE_FUNC);
	ray.Wait();

#ifndef FINAL_BUILD
	ray.DebugDraw(ICollCastJob::DrawConfig(kPrimDuration1FramePauseable));
#endif

	Point heelColPoint(animPosition.foot.GetHeelPoint());
	Point ballColPoint(animPosition.foot.GetBallPoint());
	Vector heelColNormal = animPosition.groundNormal;
	Vector ballColNormal = animPosition.groundNormal;
	bool heelHit = false;
	bool ballHit = false;
	const float minYNormal = 0.707f;
	if (ray.IsContactValid(0,0))
	{
		heelColPoint = ray.GetContactPoint(0,0);
		heelColNormal = LimitGroundNormal(ray.GetContactNormal(0,0), minYNormal);
		heelHit = true;
	}
	if (ray.IsContactValid(1,0))
	{
		ballColPoint = ray.GetContactPoint(1,0);
		ballColNormal = LimitGroundNormal(ray.GetContactNormal(1,0), minYNormal);
		ballHit = true;
	}
	g_prim.Draw(DebugCross(heelColPoint, 0.05f), kPrimDuration1FramePauseable);
	g_prim.Draw(DebugString(heelColPoint, StringBuilder<128>("Heel delta: %f\n", (float)(heelColPoint.Y() - animPosition.foot.GetHeelPoint().Y())).c_str()), kPrimDuration1FramePauseable);
	g_prim.Draw(DebugCross(ballColPoint, 0.05f), kPrimDuration1FramePauseable);
	g_prim.Draw(DebugString(ballColPoint, StringBuilder<128>("Ball delta: %f\n", (float)(ballColPoint.Y() - animPosition.foot.GetBallPoint().Y())).c_str()), kPrimDuration1FramePauseable);

	if (ballHit && !heelHit)
	{
		heelColNormal = ballColNormal;
		heelColPoint = animPosition.foot.GetHeelPoint() + (ballColPoint - animPosition.foot.GetBallPoint());
	}
	else if (heelHit && !ballHit)
	{
		ballColNormal = heelColNormal;
		ballColPoint = animPosition.foot.GetBallPoint() + (heelColPoint - animPosition.foot.GetHeelPoint());
	}

	float heelDelta = heelColPoint.Y() - animPosition.foot.GetHeelPoint().Y();
	float ballDelta = ballColPoint.Y() - animPosition.foot.GetHeelPoint().Y();

	if (ballDelta > heelDelta)
	{
		//Project the heel position to the ground normal plane
		Point heelPlaneProj;
		LinePlaneIntersect(ballColPoint, ballColNormal, animPosition.foot.GetHeelPoint(), animPosition.foot.GetHeelPoint()+kUnitYAxis, nullptr, &heelPlaneProj);
		Point heelPointOnGround = ballColPoint + Normalize(heelPlaneProj - ballColPoint)*animPosition.foot.footLength;
		Quat rot = QuatFromVectors(animPosition.groundNormal, ballColNormal);
		collisionPosition.foot.loc.SetTranslation(heelPointOnGround);
		collisionPosition.foot.loc.SetRotation(rot*collisionPosition.foot.loc.GetRotation());
		collisionPosition.groundNormal = ballColNormal;
	}
	else
	{
		collisionPosition.foot.loc.SetTranslation(animPosition.foot.GetHeelPoint() + Vector(0.0f, heelDelta, 0.0f));
		Quat rot = QuatFromVectors(animPosition.groundNormal, heelColNormal);
		collisionPosition.foot.loc.SetRotation(rot*collisionPosition.foot.loc.GetRotation());
		collisionPosition.groundNormal = heelColNormal;
	}
	/*
	if (ray.IsContactValid(0,0))
	{
		collisionPositionHeel = ray.GetContactPoint(0,0);
		collisionPosition.foot.loc.SetTranslation(collisionPositionHeel + Vector(0.0f, animPosition.footHeightOffGround, 0.0f));
		collisionPosition.groundNormal = ray.GetContactNormal(0,0);
	}
	*/
	Vector footDelta = collisionPosition.foot.GetHeelPoint() + Vector(0.0f, animPosition.footHeightOffGround, 0.0f) - animPosition.foot.GetHeelPoint();
	collisionPosition.align.SetTranslation(animPosition.align.GetTranslation() + footDelta);
	return collisionPosition;
}

static FootPlantPositions CastFootPlantsToGround(const FootPlantPositions& animPlantPositions)
{
	FootPlantPositions collisionPositions;
	if (animPlantPositions.start.Valid())
	{
		collisionPositions.start = CastFootPlantPositionToGround(animPlantPositions.start.Get());
	}
	if (animPlantPositions.end.Valid())
	{
		collisionPositions.end = CastFootPlantPositionToGround(animPlantPositions.end.Get());
	}
	return collisionPositions;
}

static FootPlantPosition CastFootPlantPositionToAnimGround(const FootPlantPosition& animPosition)
{
	FootPlantPosition collisionPosition = animPosition;

	collisionPosition.foot.loc.SetTranslation(collisionPosition.foot.loc.GetTranslation() - Vector(0.0f, animPosition.footHeightOffGround, 0.0f));
	return collisionPosition;
}

static FootPlantPositions CastFootPlantsToAnimGround(const FootPlantPositions& animPlantPositions)
{
	FootPlantPositions collisionPositions;
	if (animPlantPositions.start.Valid())
	{
		collisionPositions.start = CastFootPlantPositionToAnimGround(animPlantPositions.start.Get());
	}
	if (animPlantPositions.end.Valid())
	{
		collisionPositions.end = CastFootPlantPositionToAnimGround(animPlantPositions.end.Get());
	}
	return collisionPositions;
}

static void DrawFootPlantPosition(const FootPlantPosition& position, Color color)
{
	//g_prim.Draw(DebugSphere(position.ankle, 0.05f, color), kPrimDuration1FramePauseable);
	//
	g_prim.Draw(DebugLine(position.foot.GetHeelPoint(), position.foot.GetBallPoint(),color), kPrimDuration1FramePauseable);
	g_prim.Draw(DebugLine(position.foot.GetHeelPoint(), position.align.GetTranslation(),color), kPrimDuration1FramePauseable);
}

static Point EvaluateGroundHeight(const FootPlantPosition& plant, const FootBase& animFoot)
{
	Point pos(animFoot.GetHeelPoint());
	Point pointOnPlane(pos);
	LinePlaneIntersect(plant.align.GetTranslation(), plant.groundNormal, pos, pos+kUnitYAxis, nullptr, &pointOnPlane);
	return pointOnPlane;
}

static Point EvaluateGroundHeight(const FootPlantPositions& plantPositions, float swingPhase)
{
	if (plantPositions.start.Valid() && plantPositions.end.Valid())
	{
		Vector startToEnd = plantPositions.end.Get().align.GetTranslation() - plantPositions.start.Get().align.GetTranslation();
		Vector startTangent = startToEnd - plantPositions.start.Get().groundNormal*Dot(startToEnd, plantPositions.start.Get().groundNormal);
		Vector endTangent = -startToEnd - plantPositions.end.Get().groundNormal*Dot(-startToEnd, plantPositions.end.Get().groundNormal);

		return Lerp (plantPositions.start.Get().align.GetTranslation() + SafeNormalize(startTangent, kZero)*Length(startToEnd)*swingPhase,
			plantPositions.end.Get().align.GetTranslation() + SafeNormalize(endTangent, kZero)*Length(startToEnd)*(1-swingPhase),
			-Cos(PI*swingPhase)/2.0f+0.5f);

	}
	return kOrigin;
}

static float GetFootCurveT(const FootPlantPositions& plantPositions,  const FootBase& animFoot)
{
	if (plantPositions.start.Valid() && plantPositions.end.Valid())
	{
		Vector startToEnd = plantPositions.end.Get().foot.GetHeelPoint() - plantPositions.start.Get().foot.GetHeelPoint();
		Vector startTangent = startToEnd - plantPositions.start.Get().groundNormal*Dot(startToEnd, plantPositions.start.Get().groundNormal);
		Vector endTangent = -startToEnd - plantPositions.end.Get().groundNormal*Dot(-startToEnd, plantPositions.end.Get().groundNormal);

		Vector startToPos = animFoot.GetHeelPoint() - plantPositions.start.Get().foot.GetHeelPoint();
		float rawT = Dot(VectorXz(startToPos), VectorXz(startToEnd))/LengthXz(startToEnd)/LengthXz(startToEnd);
		float t = Limit01(rawT);
		return t;
	}
	return -1.0f;
}

static Point EvaluateGroundHeight(const FootPlantPositions& plantPositions,  const FootBase& animFoot)
{
	if (plantPositions.start.Valid() && plantPositions.end.Valid())
	{
		float t = GetFootCurveT(plantPositions, animFoot);
		return EvaluateGroundHeight(plantPositions, t);
	}
	else if (plantPositions.start.Valid())
	{
		return EvaluateGroundHeight(plantPositions.start.Get(), animFoot);
	}
	else if (plantPositions.end.Valid())
	{
		return EvaluateGroundHeight(plantPositions.end.Get(), animFoot);
	}

	return kOrigin;
}

static FootBase EvaluateFootBase(const FootPlantPositions& plantPositions, float t)
{
	if (plantPositions.start.Valid() && plantPositions.end.Valid())
	{
		Vector startToEnd = plantPositions.end.Get().foot.GetBallPoint() - plantPositions.start.Get().foot.GetBallPoint();
		Vector startTangent = startToEnd - plantPositions.start.Get().groundNormal*Dot(startToEnd, plantPositions.start.Get().groundNormal);
		Vector endTangent = -startToEnd - plantPositions.end.Get().groundNormal*Dot(-startToEnd, plantPositions.end.Get().groundNormal);

		Point midPoint(plantPositions.start.Get().foot.GetBallPoint() + startToEnd/2.0f);
		Point midProjStart, midProjEnd;
		LinePlaneIntersect(plantPositions.start.Get().foot.GetBallPoint(), plantPositions.start.Get().groundNormal, midPoint, midPoint+kUnitYAxis, nullptr, &midProjStart);
		LinePlaneIntersect(plantPositions.end.Get().foot.GetBallPoint(), plantPositions.end.Get().groundNormal, midPoint, midPoint+kUnitYAxis, nullptr, &midProjEnd);
		float height = Max(midProjStart.Y() - midPoint.Y(), midProjEnd.Y() - midPoint.Y());


		Locator newFootLoc = Lerp(plantPositions.start.Get().foot.loc, plantPositions.end.Get().foot.loc, t);
		newFootLoc.SetTranslation(newFootLoc.GetTranslation()+Vector(0.0f, Sin(t*PI)*height*2.0f/PI, 0.0f));
		FootBase result = plantPositions.start.Get().foot;
		result.loc = newFootLoc;
		return result;
	}
	return FootBase();
}

static FootBase EvaluateFootBase(const FootPlantPosition& plantPosition, const FootBase& animFoot)
{
	//Project the animated foot to the plane of the ground plant position

	Point projPos(animFoot.GetHeelPoint());
	LinePlaneIntersect(plantPosition.foot.GetHeelPoint(), plantPosition.groundNormal, animFoot.GetHeelPoint(), animFoot.GetHeelPoint()+kUnitYAxis, nullptr, &projPos);
	FootBase result = animFoot;
	result.loc.SetTranslation(projPos);
	Quat deltaRot = QuatFromVectors(GetLocalY(animFoot.loc), GetLocalY(plantPosition.foot.loc));
	result.loc.SetRotation(deltaRot*animFoot.loc.GetRotation());
	//result.loc.SetRotation(colFoot.loc.GetRotation());
	return result;
}

static FootBase EvaluateFootBase(const FootPlantPositions& plantPositions, const FootBase& animFoot)
{
	if (plantPositions.start.Valid() && plantPositions.end.Valid())
	{
		float t = GetFootCurveT(plantPositions, animFoot);

		FootBase colFoot = EvaluateFootBase(plantPositions, t);
		FootBase resultFoot = animFoot;
		Point resultPoint = animFoot.loc.GetTranslation();
		resultPoint.SetY(colFoot.loc.GetTranslation().Y());
		resultFoot.loc.SetTranslation(resultPoint);
		g_prim.Draw(DebugLine(resultPoint, GetLocalY(colFoot.loc), kColorCyan), kPrimDuration1FramePauseable);
		g_prim.Draw(DebugLine(resultPoint, GetLocalY(animFoot.loc), kColorMagenta), kPrimDuration1FramePauseable);
		Quat deltaRot = QuatFromVectors(GetLocalY(animFoot.loc), GetLocalY(colFoot.loc));
		resultFoot.loc.SetRotation(deltaRot*animFoot.loc.GetRotation());
		//resultFoot.loc.SetRotation(colFoot.loc.GetRotation());
		g_prim.Draw(DebugLine(resultPoint, GetLocalY(resultFoot.loc), kColorYellow), kPrimDuration1FramePauseable);
		return resultFoot;
	}
	else if (plantPositions.start.Valid())
	{
		return EvaluateFootBase(plantPositions.start.Get(), animFoot);
	}
	else if (plantPositions.end.Valid())
	{
		return EvaluateFootBase(plantPositions.end.Get(), animFoot);
	}

	return FootBase();
}

static void DrawFootPlantPositions(const FootPlantPositions& plantPositions, Color color)
{
	if (plantPositions.start.Valid())
	{
		DrawFootPlantPosition(plantPositions.start.Get(), color);
	}
	if (plantPositions.end.Valid())
	{
		DrawFootPlantPosition(plantPositions.end.Get(), color);
	}
	if (plantPositions.start.Valid() && plantPositions.end.Valid())
	{
		static const int kNumPoints = 20;
		for (int i = 1; i < kNumPoints; i++)
		{
			float t = (float)(i)/(kNumPoints-1);
			float t1 = (float)(i-1)/(kNumPoints-1);

			Point p0 = EvaluateGroundHeight(plantPositions, t);
			Point p1 = EvaluateGroundHeight(plantPositions, t1);

			g_prim.Draw(DebugLine(p0, p1, color), kPrimDuration1FramePauseable);

			g_prim.Draw(DebugLine(EvaluateFootBase(plantPositions, t).GetHeelPoint(), EvaluateFootBase(plantPositions, t1).GetHeelPoint(), color), kPrimDuration1FramePauseable);

		}
	}
}

static void DrawFootBase(const FootBase& foot, Color color, const char* pText = nullptr)
{
	g_prim.Draw(DebugArrow(foot.GetHeelPoint(), foot.GetBallPoint(), color), kPrimDuration1FramePauseable);
	g_prim.Draw(DebugLine(foot.GetHeelPoint(), GetLocalY(foot.loc), color), kPrimDuration1FramePauseable);
	if (pText)
	{
		g_prim.Draw(DebugString(foot.GetHeelPoint(), pText, color), kPrimDuration1FramePauseable);
	}
}

static FootPlantPosition TransformFootPlantPositions(const Locator& transform, const FootPlantPosition& plant)
{
	FootPlantPosition result;
	result.align = transform.TransformLocator(plant.align);
	result.groundNormal = transform.TransformVector(plant.groundNormal);
	result.footHeightOffGround = plant.footHeightOffGround;
	result.foot = plant.foot;
	result.foot.loc = transform.TransformLocator(plant.foot.loc);
	return result;
}

static FootPlantPositions TransformFootPlantPositions(const Locator& transform, const FootPlantPositions& plants)
{
	FootPlantPositions result;
	if (plants.start.Valid())
	{
		result.start = TransformFootPlantPositions(transform, plants.start.Get());
	}
	if (plants.end.Valid())
	{
		result.end = TransformFootPlantPositions(transform, plants.end.Get());
	}
	return result;
}

float ComputeAnkleAngle(LegIkChain* pIkChain)
{
	const Vector ankleZ(GetLocalZ(pIkChain->GetAnkleLocLs().GetRotation()));
	const float angle = Atan2(ankleZ.Z(), -ankleZ.Y());
	//g_prim.Draw(DebugString(pIkChain->GetAnkleLocWs().GetTranslation(), StringBuilder<32>("%f", RadiansToDegrees(angle)).c_str()));
	return angle;
}

F32 FootAnalysis::SolveLegIk(const Locator& align,
							 LegIkChain** apIkChains,		   // must be an array of at least legCount length
							 Locator* aDesiredAnkleLocs,	   // must be an array of at least legCount length
							 float* aFlightArcs,			   // must be an array of at least legCount length
							 IRootBaseSmoother* pRootSmoother, // must be an array of at least legCount length
							 FootIkCharacterType charType,
							 int legCount,
							 F32 enforceHipDist /* = 0.0f */,
							 bool moveRootUp /* = false */,
							 Vector rootXzOffset /* = Vector(kZero) */,
							 float* pDesiredRootOffset /*= nullptr */)
{
	float desiredRootDelta[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float maxRootDelta[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	
	ANIM_ASSERT(legCount == GetLegCountForCharacterType(charType));

	const Point rootPos = apIkChains[0]->GetRootLocWs().GetTranslation();

	F32 heightAboveAlign = 0.0f;
	bool onlyDown = false;
	for (int i = 0; i < legCount; i++)
	{
		//DrawFootBase(desiredFootBase[i], kColorYellow, "desired foot");
		//desiredAnkleLoc[i] = desiredFootBase[i].loc.TransformLocator(animFootBase[i].loc.UntransformLocator(ikChains[i].GetAnkleLocWs()));

		const Point hipPosWs   = apIkChains[i]->GetHipLocWs().Pos();
		const Point anklePosWs = apIkChains[i]->GetAnkleLocWs().Pos();

		const LegDeltaLimits limits = ComputeMaxLegRootDelta(align, *apIkChains[i], anklePosWs);

		const Point desAnklePosWs = aDesiredAnkleLocs[i].Pos();

		desiredRootDelta[i] = desAnklePosWs.Y() + limits.m_desired - hipPosWs.Y();
		maxRootDelta[i] = desAnklePosWs.Y() + limits.m_max - hipPosWs.Y();
		F32 deltaAboveAlign = anklePosWs.Y() - align.GetTranslation().Y();
		if (deltaAboveAlign > heightAboveAlign)
		{
			heightAboveAlign = deltaAboveAlign;
		}

		if (desiredRootDelta[i] < -limits.m_give && !moveRootUp)
		{
			onlyDown = true;
		}

		if (FALSE_IN_FINAL_BUILD(s_debugDraw))
		{
			g_prim.Draw(DebugCoordAxesLabeled(aDesiredAnkleLocs[i], "Desired ankle loc", 0.2f), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCross(Point(aDesiredAnkleLocs[i].GetTranslation().X(), rootPos.Y() + desiredRootDelta[i], aDesiredAnkleLocs[i].GetTranslation().Z()), 0.3f, kColorGreen), kPrimDuration1FramePauseable);
			g_prim.Draw(DebugCross(Point(aDesiredAnkleLocs[i].GetTranslation().X(), rootPos.Y() + maxRootDelta[i], aDesiredAnkleLocs[i].GetTranslation().Z()), 0.3f, kColorRed), kPrimDuration1FramePauseable);

			MsgCon("    Flight Phase:       %+f\n", aFlightArcs[i]);
			MsgCon("    Desired root delta: %+f\n", desiredRootDelta[i]);
			MsgCon("    Max root delta:     %+f\n", maxRootDelta[i]);
		}
	}

	float maxDelta = -kLargeFloat;
	float desiredDelta = kLargeFloat;
	float meanDesiredDelta = 0.0f;
	for (int iLeg = 0; iLeg < legCount; ++iLeg)
	{
		if (onlyDown && desiredRootDelta[iLeg] > 0.0f)
			continue;

		maxDelta = Max(maxDelta, maxRootDelta[iLeg]);
		desiredDelta = Min(desiredDelta, desiredRootDelta[iLeg]);
		meanDesiredDelta += desiredRootDelta[iLeg];

	}
	meanDesiredDelta /= legCount;

	float minToAvg = meanDesiredDelta - desiredDelta;
	float minToMax = maxDelta - desiredDelta;

	if (moveRootUp && (desiredDelta < 0.0f))
	{
		desiredDelta = 0.0f;
	}

	float finalDelta = desiredDelta
					   + ((minToAvg * minToMax == 0.0f) ? 0.0f : minToAvg * minToMax / (minToAvg + minToMax));


	if (moveRootUp && (finalDelta < 0.0f) && heightAboveAlign > 0.0f)
	{
		finalDelta = 0.0f;
	}

	ASSERT(IsFinite(finalDelta));

	if (pDesiredRootOffset)
		finalDelta = *pDesiredRootOffset;

	float smoothedDelta = finalDelta;

	if (pRootSmoother)
	{
		float desiredRootBaseY = align.GetTranslation().Y() + finalDelta;
		float maxRootBaseY = align.GetTranslation().Y() + maxDelta;
		float smoothedRootBaseY = pRootSmoother->SmoothY(desiredRootBaseY, maxRootBaseY);

		smoothedDelta = smoothedRootBaseY - align.GetTranslation().Y();
	}

	//Lift the feet up if the smoother pushed us higher than the max
	for (int i = 0; i < legCount; i++)
	{
		if (smoothedDelta > maxRootDelta[i])
		{
			aDesiredAnkleLocs[i].SetTranslation(aDesiredAnkleLocs[i].GetTranslation() + Vector(0.0f, smoothedDelta - maxRootDelta[i], 0.0f));
		}
	}

	if (FALSE_IN_FINAL_BUILD(s_debugDraw))
	{
		MsgCon("Final desired root delta     %+f\n", finalDelta);
		MsgCon("Final smoothed root delta     %+f\n", smoothedDelta);
	}

	for (int i = 0; i < legCount; i++)
	{
		rootXzOffset.SetY(smoothedDelta);
		apIkChains[i]->TranslateRootWs(/*Vector(0.0f, smoothedDelta, 0.0f)*/rootXzOffset);

		float distHipToAnkle = Dist(apIkChains[i]->GetAnkleLocWs().GetTranslation(), apIkChains[i]->GetHipLocWs().GetTranslation());
		float distHipToDesiredAnkle =  Dist(aDesiredAnkleLocs[i].GetTranslation(), apIkChains[i]->GetHipLocWs().GetTranslation());

		if ((distHipToAnkle < distHipToDesiredAnkle) && false)
		{
			ScopedTempAllocator alloc(FILE_LINE_FUNC);

			Locator ballInAnkleSpace = apIkChains[i]->GetAnkleLocWs().UntransformLocator(apIkChains[i]->GetBallLocWs());
			Locator heelInAnkleSpace = apIkChains[i]->GetAnkleLocWs().UntransformLocator(apIkChains[i]->GetHeelLocWs());
			Locator desiredBall = aDesiredAnkleLocs[i].TransformLocator(ballInAnkleSpace);
			Locator desiredHeel = aDesiredAnkleLocs[i].TransformLocator(heelInAnkleSpace);

			const LegIkChain::JointName2DArray* pJointNames = LegIkChain::GetJointIdsForCharType(charType);
			ANIM_ASSERT(pJointNames);

			JacobianMap::EndEffectorDef feetEffs[] = {
				JacobianMap::EndEffectorDef((*pJointNames)[i][LegIkChain::kBall], IkGoal::kPosition),

				// was already commented out before quadruped rewrite
				//JacobianMap::EndEffectorDef(LegIkChain::kJointIds[i][LegIkChain::kBall], IkGoal::kRotation),
			};
			JacobianMap m_ikJacobianMap;
			JointLimits	m_jointLimits;
			m_ikJacobianMap.Init(apIkChains[i], (*pJointNames)[i][LegIkChain::kHip], ARRAY_COUNT(feetEffs), feetEffs);
			m_jointLimits.SetupJoints(apIkChains[i]->GetNdGameObject(), SID("*player-ik-foot-plant*")); //could have different foot ik settings

			JacobianIkInstance instance;
			instance.m_pJoints = apIkChains[i];
			instance.m_pJacobianMap = &m_ikJacobianMap;
			instance.m_pJointLimits = &m_jointLimits;
			instance.m_maxIterations = 10;
			instance.m_restoreFactor = 0.0f;
			instance.m_debugDrawJointLimits = true;


			//instance.m_updateAllJointLocs = false;
			instance.m_goal[0].SetGoalPosition(desiredBall.GetTranslation());
			//instance.m_goal[1].SetGoalRotation(desiredBall.GetRotation());

			SolveJacobianIK(&instance);

			//Blend out desired rotation when the foot is in the air
			float rotationCorrection = 1.0f - aFlightArcs[i];
			const Quat qBallPostIk = apIkChains[i]->GetBallLocWs().GetRotation();
			const Quat qBallDeltaRot = Normalize(desiredBall.GetRotation() * Conjugate(qBallPostIk));
			ALWAYS_ASSERT(IsNormal(qBallDeltaRot));
			apIkChains[i]->RotateBallWs(Slerp(kIdentity, qBallDeltaRot, rotationCorrection));
		}
		else
		{

			ASSERT(IsFinite(aDesiredAnkleLocs[i].GetTranslation()));
			ASSERT(IsFinite(aDesiredAnkleLocs[i].GetRotation()));
			LegIkInstance instance;
			instance.m_ikChain = apIkChains[i];

			Point originalAnkle = apIkChains[i]->GetAnkleLocWs().GetTranslation();

			Point goal = aDesiredAnkleLocs[i].GetTranslation();

			if (enforceHipDist > 0.0f)
			{
				Point hipPoint = apIkChains[i]->GetHipLocWs().GetTranslation();
				if (goal.Y() + enforceHipDist > hipPoint.Y())
				{
					goal.SetY(hipPoint.Y() - enforceHipDist);
				}
			}

			instance.m_goalPos = goal;
			const Quat qAnklePreIk = aDesiredAnkleLocs[i].GetRotation();
			
			const float animAngle = ComputeAnkleAngle(apIkChains[i]);
			::SolveLegIk(&instance);

			//Blend out desired rotation when the foot is in the air
			const Quat qAnklePostIk = apIkChains[i]->GetAnkleLocWs().GetRotation();

			const Quat qAnkleDeltaRot = Normalize(qAnklePreIk * Conjugate(qAnklePostIk));
			ALWAYS_ASSERT(IsNormal(qAnkleDeltaRot));

			float ankleCorrectionTT = 1.0f - aFlightArcs[i];
			apIkChains[i]->RotateAnkleWs(Slerp(kIdentity, qAnkleDeltaRot, ankleCorrectionTT));

			const float postIkAngle = ComputeAnkleAngle(apIkChains[i]);

			//TODO expose this hardcoded angle
			const float minAngle = Min(animAngle, DegreesToRadians(45.0f));
			if (postIkAngle < minAngle && false)
			{
				//Rotate around the ball joint to give the ankle a more reasonable angle
				const float angleDiff = minAngle - postIkAngle;
				const Locator ballWs = apIkChains[i]->GetBallLocWs();
				const Locator ankleWs = apIkChains[i]->GetAnkleLocWs();
				const Vector ballZ(GetLocalZ(ballWs));
				float angle = -angleDiff;
				const Locator desiredBallWS(ballWs.GetTranslation(), ballWs.GetRotation()*QuatFromAxisAngle(kUnitXAxis, angle));
				const Locator desiredAnkleWs = desiredBallWS.TransformLocator(ballWs.UntransformLocator(ankleWs));

				//g_prim.Draw(DebugCoordAxes(desiredBallWS), kPrimDuration1FramePauseable);
				//g_prim.Draw(DebugCoordAxes(desiredAnkleWs), kPrimDuration1FramePauseable);
				{
					instance.m_goalPos = desiredAnkleWs.GetTranslation();
					::SolveLegIk(&instance);
					apIkChains[i]->RotateAnkleWs(desiredAnkleWs.GetRotation()*Conjugate(apIkChains[i]->GetAnkleLocWs().GetRotation()));
					apIkChains[i]->RotateBallWs(ballWs.GetRotation()*Conjugate(apIkChains[i]->GetBallLocWs().GetRotation()));
				}
				ComputeAnkleAngle(apIkChains[i]);
			}

			ASSERT(IsFinite(apIkChains[i]->GetAnkleLocWs()));
			ASSERT(IsFinite(apIkChains[i]->GetBallLocWs()));

		}
	}

	return smoothedDelta;
}

class SimpleFootPrediction : public FootAnalysis::IFootPrediction
{
public:
	SimpleFootPrediction(const FootPlantPositions& positions)
		: m_positions(positions)
	{}

	Point EvaluateGroundPoint(const FootBase& animFoot) override
	{
		return EvaluateGroundHeight(m_positions, animFoot);
	}

	FootBase EvaluateFoot(const FootBase& animFoot) override
	{
		return EvaluateFootBase(m_positions, animFoot);
	}

private:
	const FootPlantPositions m_positions;
};

void FootAnalysis::TestFootIk( NdGameObject* pGo, const ArtItemAnim* pAnim, float phase, bool mirror, bool enableCollision )
{
	ScopedTempAllocator aloc(FILE_LINE_FUNC);

	const int legCount = pGo->GetLegCount();

	LegIkChain ikChains[kQuadLegCount];
	for (int i = 0; i < legCount; i++)
	{
		ikChains[i].Init(pGo, i, true);
		ikChains[i].InitIkData(SID("*player-ik-foot-plant*")); // allow setting different foot plant settings?
		ikChains[i].ReadJointCache();
	}

	Locator alignToAnimSpace(kIdentity);
	EvaluateChannelInAnim(pGo->GetSkeletonId(), pAnim, SID("align"), phase, &alignToAnimSpace, mirror);
	Locator animSpaceToWorldSpace = pGo->GetLocator().TransformLocator(Inverse(alignToAnimSpace));

	Plane animGroundPlane(pGo->GetTranslation(), kUnitYAxis);
	Locator groundApRefAlignSpace(kIdentity);
	bool groundValid = EvaluateChannelInAnim(pGo->GetSkeletonId(), pAnim, SID("apReference-ground"), phase, &groundApRefAlignSpace, mirror);
	if (groundValid)
	{
		animGroundPlane = Plane(pGo->GetLocator().TransformPoint(groundApRefAlignSpace.GetTranslation()), pGo->GetLocator().TransformVector(GetLocalY(groundApRefAlignSpace)));
	}

	FeetIkData feetData = GetFeetDataFromAnim(pAnim, phase, mirror, pGo->GetFootIkCharacterType());

	LegIkChain* apIkChains[] = {&ikChains[0], &ikChains[1], &ikChains[2], &ikChains[3]};

	FeetPlantPositions feetPositions = GetFootPlantPositionsFromAnimWs(pAnim, phase, mirror, animSpaceToWorldSpace, pGo->GetFootIkCharacterType());
	FeetPlantPositions colFeetPositions;
	for (int i = 0; i < legCount; i++)
	{
		colFeetPositions.m_plants[i] = enableCollision ? CastFootPlantsToGround(feetPositions.m_plants[i]) : CastFootPlantsToAnimGround(feetPositions.m_plants[i]);
	}
	SimpleFootPrediction leftPred(colFeetPositions.m_plants[kLeftLeg]);
	SimpleFootPrediction rightPred(colFeetPositions.m_plants[kRightLeg]);
	SimpleFootPrediction frontLeftPred(colFeetPositions.m_plants[kFrontLeftLeg]);
	SimpleFootPrediction frontRightPred(colFeetPositions.m_plants[kFrontRightLeg]);

	IFootPrediction* apPredictions[] = {&leftPred, &rightPred, &frontLeftPred, &frontRightPred};

	DoFootIk(
		pGo,
		pAnim,
		phase,
		mirror,
		enableCollision,
		apIkChains,
		animGroundPlane,
		animSpaceToWorldSpace,
		feetData,
		apPredictions,
		nullptr,
		legCount);

	for (int i = 0; i < legCount; i++)
	{
		ikChains[i].WriteJointCache();
	}

	/*
		MsgCon("Foot %s:\n", footNames[i]);
		MsgCon("    Swing Phase:        %+f\n", swingPhase[i]);
		MsgCon("    Flight Phase:       %+f\n", flightPhase[i]);
		MsgCon("    Foot Delta:         %+f\n", footGroundHeight[i]);
		MsgCon("    Ground Height       %+f\n", groundHeight[i]);
		MsgCon("    Root weight:        %+f\n", groundWeight[i]);
		MsgCon("    Desired root delta: %+f\n", desiredRootDelta[i]);
		MsgCon("    Max root delta:     %+f\n", maxRootDelta[i]);
		MsgCon("    Foot adjustment:    %+f\n", float(desiredFootBase[i].GetHeelPoint().Y() - animFootBase[i].GetHeelPoint().Y()));
	*/

}




void FootAnalysis::DoFootIk(
	NdGameObject* pGo,
	const ArtItemAnim* pAnim,
	float phase,
	bool mirror,
	bool enableCollision,
	LegIkChain** apIkChains,			// must be an array of at least legCount length
	const Plane& animGroundPlaneWs,
	const Locator animSpaceToWorldSpace,
	const FeetIkData& feetIkData,
	IFootPrediction** apPredictions,	// must be an array of at least legCount length
	IRootBaseSmoother* pRootSmoother,
	int legCount)
{
	ANIM_ASSERT(pGo);

	const FootIkCharacterType charType = pGo->GetFootIkCharacterType();
	ANIM_ASSERT(legCount == pGo->GetLegCount());

	if (!AddFootChannelsToAnim(pAnim, charType))
	{
		return;
	}

	ScopedTempAllocator aloc(FILE_LINE_FUNC);


	float groundHeight[] = {0.0f, 0.0f, 0.0f, 0.0f};
	float footGroundHeight[] = {0.0f, 0.0f, 0.0f, 0.0f};
	StringId64 ankleIds[] = { GetJointName(charType, kFootIkJointLeftAnkle), GetJointName(charType, kFootIkJointRightAnkle), GetJointName(charType, kFootIkJointFrontLeftAnkle), GetJointName(charType, kFootIkJointFrontRightAnkle) };
	StringId64 ballIds[] = { GetJointName(charType, kFootIkJointLeftBall), GetJointName(charType, kFootIkJointRightBall), GetJointName(charType, kFootIkJointFrontLeftBall), GetJointName(charType, kFootIkJointFrontRightBall) };
	StringId64 heelIds[] = { GetJointName(charType, kFootIkJointLeftHeel), GetJointName(charType, kFootIkJointRightHeel), GetJointName(charType, kFootIkJointFrontLeftHeel), GetJointName(charType, kFootIkJointFrontRightHeel) };
	StringId64 ankleChannelIds[] = { GetAnkleChannelName(charType, kLeftLeg), GetAnkleChannelName(charType, kRightLeg), GetAnkleChannelName(charType, kFrontLeftLeg), GetAnkleChannelName(charType, kFrontRightLeg) };
	const char* footNames[] = { "left", "right", "front-left", "front-right" };
	Color colors[] = { kColorBlue, kColorRed, kColorCyan, kColorMagenta };

	FootPlantList footPlants[kQuadLegCount];
	FootBase animFootBase[kQuadLegCount];
	FootBase desiredFootBase[kQuadLegCount];
	Locator desiredAnkleLoc[kQuadLegCount];
	float footHeightOffGround[kQuadLegCount];

	float numFrames = pAnim->m_pClipData->m_fNumFrameIntervals;
	float currentFrame = phase*numFrames;

	const Vector up(animGroundPlaneWs.GetNormal());

	for (int i = 0; i < legCount; i++)
	{
		const int animAnkleIndex = mirror ? 1 - i : i;

		footPlants[i].Init(50, FILE_LINE_FUNC);
		GetEFFFootPlantsForAnim(pAnim, ballIds[animAnkleIndex], footPlants[i]);
		if (footPlants[i].Size() == 0)
		{
			return;
		}

		animFootBase[i] = GetFootBaseFromAnklePos(pGo->GetSkeletonId(), apIkChains[i]->GetAnkleLocWs(), ankleIds[i], heelIds[i], ballIds[i], animGroundPlaneWs.GetNormal());

		Point heelOnAnimPlane;
		LinePlaneIntersect(animGroundPlaneWs.ProjectPoint(animFootBase[i].GetHeelPoint()), animGroundPlaneWs.GetNormal(), animFootBase[i].GetHeelPoint(), animFootBase[i].GetHeelPoint()+kUnitYAxis, nullptr, &heelOnAnimPlane);
		footHeightOffGround[i] = animFootBase[i].GetHeelPoint().Y() - heelOnAnimPlane.Y();
		g_prim.Draw(DebugCross(heelOnAnimPlane, 0.2f, colors[i]), kPrimDuration1FramePauseable);


		groundHeight[i] = Dot(AsVector(apPredictions[i]->EvaluateGroundPoint(animFootBase[i])), up);
		FootBase collisionAdjustedFootBase = apPredictions[i]->EvaluateFoot(animFootBase[i]);
		desiredFootBase[i] = collisionAdjustedFootBase;

		DrawFootBase(animFootBase[i], colors[i], "Anim foot");
		DrawFootBase(collisionAdjustedFootBase, colors[i], "Col foot");

		footGroundHeight[i] = collisionAdjustedFootBase.GetHeelPoint().Y();

		desiredFootBase[i].loc.SetTranslation(desiredFootBase[i].loc.GetTranslation() + Vector(0.0f, footHeightOffGround[i], 0.0f));
	}

	float totalWeight = 0.0f;
	for (int iLeg = 0; iLeg < legCount; ++iLeg)
	{
		totalWeight += feetIkData.m_footData[iLeg].m_groundWeight;
	}

	float supportingGroundDelta = 0.0f;
	for (int iLeg = 0; iLeg < legCount; ++iLeg)
	{
		supportingGroundDelta += feetIkData.m_footData[iLeg].m_groundWeight * groundHeight[iLeg];
	}
	supportingGroundDelta /= totalWeight;
	g_prim.Draw(DebugCross(Point(pGo->GetTranslation().X(), supportingGroundDelta, pGo->GetTranslation().Z()), 1.0f, kColorMagenta));
	for (int i = 0; i < legCount; i++)
	{
		DrawFootBase(desiredFootBase[i], kColorOrange, "desired foot");
		float flightArc = feetIkData.m_footData[i].m_flightArc;
		float liftedHeight = Lerp(groundHeight[i], supportingGroundDelta, flightArc);

		float footWeight = feetIkData.m_footData[i].m_groundWeight/totalWeight;
//  		{
//  			Point footPoint = desiredFootBase[i].loc.GetTranslation();
// 			float distAboveOtherFoot = footGroundHeight[i] - footGroundHeight[1-i];
// 			float distAboveCol = (float)footPoint.Y() -footGroundHeight[i];
//  			footPoint.SetY( footPoint.Y() - Max(Min(distAboveCol, distAboveOtherFoot), 0.0f)*flightArc);
//  			desiredFootBase[i].loc.SetTranslation(footPoint);
//  		}

// 		if (liftedHeight > groundHeight[i])
// 		{
// 			Point footPoint = desiredFootBase[i].loc.GetTranslation();
// 			footPoint.SetY(footPoint.Y() + (liftedHeight-groundHeight[i]));
// 			ASSERT(desiredFootBase[i].loc.GetTranslation().Y() <= footPoint.Y());
// 			desiredFootBase[i].loc.SetTranslation(footPoint);
// 		}
	}

	for (int i = 0; i < legCount; i++)
	{
		DrawFootBase(desiredFootBase[i], kColorYellow, "desired foot");
		desiredAnkleLoc[i] = desiredFootBase[i].loc.TransformLocator(animFootBase[i].loc.UntransformLocator(apIkChains[i]->GetAnkleLocWs()));
	}

	float flightArc[] = {feetIkData.m_footData[kLeftLeg].m_flightArc, feetIkData.m_footData[kRightLeg].m_flightArc, feetIkData.m_footData[kFrontLeftLeg].m_flightArc, feetIkData.m_footData[kFrontRightLeg].m_flightArc};

	SolveLegIk(
		pGo->GetLocator(),
		apIkChains,
		desiredAnkleLoc,
		flightArc,
		pRootSmoother,
		charType,
		legCount);

	/*
		MsgCon("Foot %s:\n", footNames[i]);
		MsgCon("    Swing Phase:        %+f\n", swingPhase[i]);
		MsgCon("    Flight Phase:       %+f\n", flightPhase[i]);
		MsgCon("    Foot Delta:         %+f\n", footGroundHeight[i]);
		MsgCon("    Ground Height       %+f\n", groundHeight[i]);
		MsgCon("    Root weight:        %+f\n", groundWeight[i]);
		MsgCon("    Desired root delta: %+f\n", desiredRootDelta[i]);
		MsgCon("    Max root delta:     %+f\n", maxRootDelta[i]);
		MsgCon("    Foot adjustment:    %+f\n", float(desiredFootBase[i].GetHeelPoint().Y() - animFootBase[i].GetHeelPoint().Y()));
	*/
}



FootAnalysis::FootIkData Lerp(const FootAnalysis::FootIkData& a, const FootAnalysis::FootIkData& b, float t)
{
	FootAnalysis::FootIkData result;
	result.m_flightArc = Lerp(a.m_flightArc, b.m_flightArc, t);
	result.m_groundWeight = Lerp(a.m_groundWeight, b.m_groundWeight, t);
	return result;
}

FootAnalysis::FeetIkData Lerp(const FootAnalysis::FeetIkData& a, const FootAnalysis::FeetIkData& b, float t)
{
	FootAnalysis::FeetIkData result;
	for (int iLeg = 0; iLeg < kQuadLegCount; ++iLeg)
	{
		result.m_footData[iLeg] = Lerp(a.m_footData[iLeg], b.m_footData[iLeg], t);
	}
	return result;
}

FootAnalysis::FeetIkData FootAnalysis::GetFeetDataFromAnim(const ArtItemAnim* pAnim, float phase, bool mirror, FootIkCharacterType charType)
{
	//These should be precomputed in a custom channel
	FootAnalysis::FeetIkData result;

	if (pAnim)
	{
		float numFrames = pAnim->m_pClipData->m_fNumFrameIntervals;
		float currentFrame = phase*numFrames;

		const StringId64 ballIds[] = { GetJointName(charType, kFootIkJointLeftBall), GetJointName(charType, kFootIkJointRightBall), GetJointName(charType, kFootIkJointFrontLeftBall), GetJointName(charType, kFootIkJointFrontRightBall) };
		const int legCount = GetLegCountForCharacterType(charType);

		for (int iLeg = 0; iLeg < legCount; ++iLeg)
		{
			ScopedTempAllocator alloc(FILE_LINE_FUNC);
			FootPlantList footPlants;
			footPlants.Init(50, FILE_LINE_FUNC);
			GetEFFFootPlantsForAnim(pAnim, ballIds[iLeg], footPlants);
			if (footPlants.Size() == 0)
				return FootAnalysis::FeetIkData();
			const float swingPhase = ComputeSwingPhaseFromFootPlants(footPlants, numFrames, currentFrame);
			const float groundWeight = GroundHeightWeightFromSwingPhase(swingPhase);
			const float flightPhase = ComputeFlightPhaseFromFootPlants(footPlants, numFrames, currentFrame);
			int outIndex = mirror ? GetMatchingLegIndex(iLeg) : iLeg;
			ANIM_ASSERTF(outIndex >= 0 && outIndex < legCount, ("outIndex: %d, legCount: %d, mirror: %s", outIndex, legCount, mirror ? "YES" : "NO"));
			result.m_footData[outIndex].m_groundWeight = groundWeight;
			result.m_footData[outIndex].m_flightArc = Sin(flightPhase * PI);
		}
	}
	return result;
}

class FeetIkDataInstanceBlender : public AnimStateLayer::InstanceBlender<FootAnalysis::FeetIkData>
{
public:
	typedef AnimStateLayer::InstanceBlender<FootAnalysis::FeetIkData> ParentClass;
	FootIkCharacterType m_charType;

	FeetIkDataInstanceBlender() : ParentClass(), m_charType(kFootIkCharacterTypeHuman) {}

protected:
	FootAnalysis::FeetIkData GetDefaultData() const override
	{
		return FootAnalysis::FeetIkData();
	}

	bool GetDataForInstance(const AnimStateInstance* pInstance, FootAnalysis::FeetIkData* pDataOut) override
	{
		*pDataOut = FootAnalysis::GetFeetDataFromAnim(pInstance->GetPhaseAnimArtItem().ToArtItem(), pInstance->Phase(), pInstance->IsFlipped(), m_charType);
		return true;
	}

	FootAnalysis::FeetIkData BlendData(const FootAnalysis::FeetIkData& leftData, const FootAnalysis::FeetIkData& rightData, float masterFade, float animFade, float motionFade) override
	{
		return Lerp(leftData, rightData, animFade);
	}
};

FootAnalysis::FeetIkData FootAnalysis::GetFootData(NdGameObject* pGo)
{
	PROFILE_AUTO(IK);
	FeetIkDataInstanceBlender blender;
	blender.m_charType = pGo->GetFootIkCharacterType();
	return blender.BlendForward(pGo->GetAnimControl()->GetBaseStateLayer(), FootAnalysis::FeetIkData());
}

FootAnalysis::FeetPlantPositions FootAnalysis::GetFootPlantPositionsFromAnimWs(const ArtItemAnim* pAnim, float phase, bool mirror, const Locator& animSpaceToWorldSpace, FootIkCharacterType charType)
{
	FeetPlantPositions result;

	ScopedTempAllocator aloc(FILE_LINE_FUNC);

	FootPlantList footPlants[kQuadLegCount];

	float numFrames = pAnim->m_pClipData->m_fNumFrameIntervals;
	float currentFrame = phase*numFrames;

	const int legCount = GetLegCountForCharacterType(charType);
	const LegIkChain::JointName2DArray* pJointNames = LegIkChain::GetJointIdsForCharType(charType);
	ANIM_ASSERT(pJointNames);

	for (int iLeg = 0; iLeg < legCount; ++iLeg)
	{
		const int animAnkleIndex = mirror ? GetMatchingLegIndex(iLeg) : iLeg;

		footPlants[iLeg].Init(50, FILE_LINE_FUNC);
		GetEFFFootPlantsForAnim(pAnim, (*pJointNames)[animAnkleIndex][LegIkChain::kBall], footPlants[iLeg]);

		result.m_plants[iLeg] = GetFootPlantPositions(footPlants[iLeg], currentFrame, kFootbaseChannels[animAnkleIndex], pAnim, mirror);
		result.m_plants[iLeg] = TransformFootPlantPositions(animSpaceToWorldSpace, result.m_plants[iLeg]);
	}

	return result;
}

FootAnalysis::IFootPrediction* FootAnalysis::CreateSimpleFootPrediction( const FootPlantPositions& plants )
{
	return NDI_NEW SimpleFootPrediction(plants);
}

FootAnalysis::FeetPlantPositions FootAnalysis::DebugCastFeetToGround(const FeetPlantPositions& plants)
{
	FeetPlantPositions result;

	for (int iLeg = 0; iLeg < kQuadLegCount; ++iLeg)
	{
		result.m_plants[iLeg] = CastFootPlantsToGround(plants.m_plants[iLeg]);
	}

	return result;
}

