/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/human-center-of-mass.h"

#include "corelib/memory/scoped-temp-allocator.h"

#include "ndlib/anim/anim-defines.h"
#include "ndlib/anim/attach-system.h"
#include "ndlib/anim/nd-anim-util.h"
#include "ndlib/anim/skel-table.h"
#include "ndlib/render/util/prim-server-wrapper.h"
#include "ndlib/render/util/prim.h"

#include "gamelib/level/art-item-skeleton.h"
#include "gamelib/level/artitem.h"

/// --------------------------------------------------------------------------------------------------------------- ///
static HumanCenterOfMass::COMEntry s_ComTable[] = 
{
	{ SID("targRShoulder"), Point(0.0048f, 0.0085f, -0.1413f), 2.4f },
	{ SID("targRElbow"), Point(0.0034f, 0.0024f, -0.1009f), 1.7f },
	{ SID("targRWrist"), Point(0.0051f, 0.0133f, -0.0925f), 0.6f },

	// Left arm is right arm with Z and X mirrored
	{ SID("targLShoulder"), Point(-0.0048f, -0.0085f, 0.1413f), 2.4f },
	{ SID("targLElbow"), Point(-0.0034f, -0.0024f, 0.1009f), 1.7f },
	{ SID("targLWrist"), Point(-0.0051f, -0.0133f, 0.0925f), 0.6f },

	{ SID("targRUpperLeg"), Point(0.0167f, -0.0207f, -0.2170f), 12.3f },
	{ SID("targRKnee"), Point(0.0028f, -0.0189f, -0.1616f), 4.8f },
	{ SID("targRAnkle"), Point(0.0043f, 0.0616f, -0.0215f), 1.2f },

	// Left leg is the right leg with Z and Y mirrored
	{ SID("targLUpperLeg"), Point(-0.0167f, 0.0207f, 0.2170f), 12.3f },
	{ SID("targLKnee"), Point(-0.0028f, 0.0189f, 0.1616f), 4.8f },
	{ SID("targLAnkle"), Point(-0.0043f, -0.0616f, 0.0215f), 1.2f },

	{ SID("targPelvis"), Point(0.0000f, -0.0020f, -0.0075f), 14.2f },
	{ SID("targChest"), Point(0.0000f, -0.0489f, -0.0694f), 33.3f },

	{ SID("targHeadB"), Point(0.0000f, 0.0153f, 0.0377f), 6.7f },
};

/// --------------------------------------------------------------------------------------------------------------- ///
static HumanCenterOfMass::COMEntry s_ComTableJoints[] =
{
	{ SID("r_shoulder"), Point(0.0048f, 0.0085f, -0.1413f), 2.4f },
	{ SID("r_elbow"), Point(0.0034f, 0.0024f, -0.1009f), 1.7f },
	{ SID("r_wrist"), Point(0.0051f, 0.0133f, -0.0925f), 0.6f },

	// Left arm is right arm with Z and X mirrored
	{ SID("l_shoulder"), Point(-0.0048f, -0.0085f, 0.1413f), 2.4f },
	{ SID("l_elbow"), Point(-0.0034f, -0.0024f, 0.1009f), 1.7f },
	{ SID("l_wrist"), Point(-0.0051f, -0.0133f, 0.0925f), 0.6f },

	{ SID("r_upper_leg"), Point(0.0167f, -0.0207f, -0.2170f), 12.3f },
	{ SID("r_knee"), Point(0.0028f, -0.0189f, -0.1616f), 4.8f },
	{ SID("r_ankle"), Point(0.0043f, 0.0616f, -0.0215f), 1.2f },

	// Left leg is the right leg with Z and Y mirrored
	{ SID("l_upper_leg"), Point(-0.0167f, 0.0207f, 0.2170f), 12.3f },
	{ SID("l_knee"), Point(-0.0028f, 0.0189f, 0.1616f), 4.8f },
	{ SID("l_ankle"), Point(-0.0043f, -0.0616f, 0.0215f), 1.2f },

	{ SID("pelvis"), Point(0.0000f, -0.0020f, -0.0075f), 14.2f },
	{ SID("spined"), Point(0.0000f, -0.0489f, -0.0694f), 33.3f },

	{ SID("headb"), Point(0.0000f, 0.0153f, 0.0377f), 6.7f },
};

/// --------------------------------------------------------------------------------------------------------------- ///
static void DrawMass(Point_arg pos, float mass, Color color)
{
	STRIP_IN_FINAL_BUILD;

	if (false)
	{
		static float scale = 0.05f;
		float radius	   = Pow(mass, 0.333f) * scale;
		DebugDrawSphere(pos, radius, color);
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point HumanCenterOfMass::ComputeCenterOfMassWs(const AttachSystem& attachSystem,
											   const COMEntry* pWeights,
											   const int numWeights)
{
	if (pWeights)
	{
		return ComputeCenterOfMassWs(attachSystem, nullptr, pWeights, numWeights);
	}
	else
	{
		return ComputeCenterOfMassWs(attachSystem, nullptr, s_ComTable, ARRAY_COUNT(s_ComTable));
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point HumanCenterOfMass::ComputeCenterOfMassWs(const AttachSystem& attachSystem,
											   ListArray<Point>* pMassList,
											   const COMEntry* pWeights,
											   const int numWeights)
{
	float totalWt = 0.0;

	Vector weightSum(kZero);

	if (pMassList)
	{
		ASSERT(pMassList->Size() >= numWeights);
	}
	for (U32 i = 0; i < numWeights; i++)
	{
		const COMEntry& entry = pWeights[i];

		AttachIndex startInd;
		bool startValid = attachSystem.FindPointIndexById(&startInd, entry.m_jointId);

		if (startValid)
		{
			Point segCOM = attachSystem.GetLocator(startInd).TransformPoint(entry.m_comOffset);

			Vector alignToSeg = segCOM - kOrigin;
			weightSum += alignToSeg * entry.m_totalWt;
			totalWt += entry.m_totalWt;

			if (pMassList)
			{
				pMassList->at(i) = segCOM;
			}
			DrawMass(segCOM, entry.m_totalWt, kColorRed);
		}
	}

	Point bodyCOM = kOrigin + weightSum / totalWt;
	DrawMass(bodyCOM, totalWt, kColorGreen);
	return bodyCOM;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point HumanCenterOfMass::ComputeCenterOfMassFromJoints(SkeletonId skelId,
													   const Transform* aJointTransforms,
													   ListArray<Point>* pMassList)
{
	const ArtItemSkeleton* pArtSkeleton = ResourceTable::LookupSkel(skelId).ToArtItem();
	if (!pArtSkeleton)
		return kOrigin;

	float totalWt	 = 0.0;
	Vector weightSum = kZero;

	for (U32 i = 0; i < ARRAY_COUNT(s_ComTableJoints); i++)
	{
		const COMEntry& entry = s_ComTableJoints[i];

		const I32F jointIndex = FindJoint(pArtSkeleton->m_pJointDescs,
										  pArtSkeleton->m_numGameplayJoints,
										  entry.m_jointId);

		if (jointIndex >= 0)
		{
			Point segCOM = Locator(aJointTransforms[jointIndex]).TransformPoint(entry.m_comOffset);

			Vector alignToSeg = segCOM - kOrigin;
			weightSum += alignToSeg * entry.m_totalWt;
			totalWt += entry.m_totalWt;

			if (pMassList)
			{
				pMassList->at(i) = segCOM;
			}

			DrawMass(segCOM, entry.m_totalWt, kColorRed);
		}
	}

	Point bodyCOM = kOrigin + weightSum / totalWt;
	DrawMass(bodyCOM, totalWt, kColorGreen);
	return bodyCOM;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point HumanCenterOfMass::ComputeCenterOfMassFromJoints(SkeletonId skelId, const Locator* aJointTransforms)
{
	const ArtItemSkeleton* pArtSkeleton = ResourceTable::LookupSkel(skelId).ToArtItem();
	if (!pArtSkeleton)
		return kOrigin;

	float totalWt	 = 0.0;
	Vector weightSum = kZero;

	for (U32 i = 0; i < ARRAY_COUNT(s_ComTableJoints); i++)
	{
		const COMEntry& entry = s_ComTableJoints[i];

		const I32F jointIndex = FindJoint(pArtSkeleton->m_pJointDescs,
										  pArtSkeleton->m_numGameplayJoints,
										  entry.m_jointId);

		if (jointIndex >= 0)
		{
			Point segCOM = aJointTransforms[jointIndex].TransformPoint(entry.m_comOffset);

			Vector alignToSeg = segCOM - kOrigin;
			weightSum += alignToSeg * entry.m_totalWt;
			totalWt += entry.m_totalWt;

			DrawMass(segCOM, entry.m_totalWt, kColorRed);
		}
	}

	Point bodyCOM = kOrigin + ((totalWt > NDI_FLT_EPSILON) ? (weightSum / totalWt) : Vector(kZero));

	DrawMass(bodyCOM, totalWt, kColorGreen);

	return bodyCOM;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Point HumanCenterOfMass::GetCenterOfMassFromAnimation(SkeletonId skelId,
													  const ArtItemAnim* pAnim,
													  float phase,
													  bool mirror,
													  ListArray<Point>* pMassList)
{
	ScopedTempAllocator jj(FILE_LINE_FUNC);

	const ArtItemSkeleton* pArtSkeleton = ResourceTable::LookupSkel(skelId).ToArtItem();

	Transform* aJointTransforms = NDI_NEW Transform[pArtSkeleton->m_numGameplayJoints];

	AnimateFlags flags = mirror ? AnimateFlags::kAnimateFlag_Mirror : AnimateFlags::kAnimateFlag_None;
	bool valid		   = AnimateObject(Transform(kIdentity),
							   pArtSkeleton,
							   pAnim,
							   pAnim->m_pClipData->m_numTotalFrames * phase,
							   aJointTransforms,
							   nullptr,
							   nullptr,
							   nullptr,
							   flags);

	ASSERT(valid);

	return ComputeCenterOfMassFromJoints(skelId, aJointTransforms, pMassList);
}

/// --------------------------------------------------------------------------------------------------------------- ///
HumanCenterOfMass::ComPose* HumanCenterOfMass::GetCOMPoseFromAnimation(SkeletonId skelId,
																	   const ArtItemAnim* pAnim,
																	   float phase,
																	   bool mirror)
{
	ComPose* pResult = NDI_NEW ComPose();
	{
		ScopedTempAllocator jj(FILE_LINE_FUNC);

		const ArtItemSkeleton* pArtSkeleton = ResourceTable::LookupSkel(skelId).ToArtItem();

		Transform* aJointTransforms = NDI_NEW Transform[pArtSkeleton->m_numGameplayJoints];

		AnimateFlags flags = mirror ? AnimateFlags::kAnimateFlag_Mirror : AnimateFlags::kAnimateFlag_None;

		bool valid = AnimateObject(Transform(kIdentity),
								   pArtSkeleton,
								   pAnim,
								   pAnim->m_pClipData->m_numTotalFrames * phase,
								   aJointTransforms,
								   nullptr,
								   nullptr,
								   nullptr,
								   flags);

		Point com = valid ? ComputeCenterOfMassFromJoints(skelId, aJointTransforms, &pResult->m_massPos) : kOrigin;

		pResult->m_comPos = com;
		// Convert positions from align space to uprightedRootSpace.
		Vector rootOffset = AsVector(com);
		for (int i = 0; i < pResult->m_massPos.Size(); i++)
		{
			pResult->m_massPos[i] -= rootOffset;
		}
	}
	return pResult;
}

/// --------------------------------------------------------------------------------------------------------------- ///
HumanCenterOfMass::ComPose* HumanCenterOfMass::GetCOMPoseFromAttachSystem(const AttachSystem& attachSystem,
																		  Locator align)
{
	PROFILE_AUTO(Animation);
	ComPose* pResult = NDI_NEW ComPose();
	Point com		 = ComputeCenterOfMassWs(attachSystem, &pResult->m_massPos, s_ComTable, ARRAY_COUNT(s_ComTable));

	pResult->m_comPos = align.UntransformPoint(com);
	Locator poseOrigin(com, align.GetRotation());
	for (int i = 0; i < pResult->m_massPos.Size(); i++)
	{
		pResult->m_massPos[i] = poseOrigin.UntransformPoint(pResult->m_massPos[i]);
	}
	return pResult;
}

/// --------------------------------------------------------------------------------------------------------------- ///
HumanCenterOfMass::HumanCenterOfMass()
{
	m_currentBuffer = 0;

	m_states[0].m_valid = m_states[1].m_valid = false;

	for (int i = 0; i < 2; i++)
	{
		for (int j = 0; j < m_states[i].m_massList.Capacity(); j++)
		{
			m_states[i].m_massList.PushBack(kOrigin);
		}
	}
}

/// --------------------------------------------------------------------------------------------------------------- ///
void HumanCenterOfMass::Update(const AttachSystem& attachSystem)
{
	int nextBuffer			 = (m_currentBuffer + 1) % 2;
	ComState& nextState		 = m_states[nextBuffer];
	nextState.m_centerOfMass = ComputeCenterOfMassWs(attachSystem,
													 &nextState.m_massList,
													 s_ComTable,
													 ARRAY_COUNT(s_ComTable));
	nextState.m_time  = GetProcessClock()->GetCurTime();
	nextState.m_valid = true;

	const ComState& currentState = m_states[m_currentBuffer];

	if (currentState.m_valid)
	{
		Vector angMomentum = kZero;
		float deltaTime	   = (nextState.m_time - currentState.m_time).ToSeconds();
		for (int i = 0; i < currentState.m_massList.Size(); i++)
		{
			const Vector velocity = (nextState.m_massList[i] - currentState.m_massList[i]) / deltaTime;
			const Vector diff	  = (nextState.m_massList[i] - nextState.m_centerOfMass);

			angMomentum += Cross(s_ComTable[i].m_totalWt * diff, velocity);
		}
		nextState.m_angularMomentum = angMomentum;
		nextState.m_comVelocity		= (nextState.m_centerOfMass - currentState.m_centerOfMass) / deltaTime;
	}
	else
	{
		nextState.m_comVelocity		= kZero;
		nextState.m_angularMomentum = kZero;
	}

	m_currentBuffer = nextBuffer;
}

/// --------------------------------------------------------------------------------------------------------------- ///
SMath::Point HumanCenterOfMass::GetCOM() const
{
	return m_states[m_currentBuffer].m_centerOfMass;
}

/// --------------------------------------------------------------------------------------------------------------- ///
Vector HumanCenterOfMass::GetAngularMomentum() const
{
	return m_states[m_currentBuffer].m_angularMomentum;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void HumanCenterOfMass::DebugDraw() const
{
	STRIP_IN_FINAL_BUILD;

	DebugDrawSphere(GetCOM(), 0.1f, kColorGreen, kPrimDuration1FramePauseable);
	g_prim.Draw(DebugLine(GetCOM(), GetAngularMomentum(), kColorGreen), kPrimDuration1FramePauseable);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void HumanCenterOfMass::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	m_states[0].m_massList.Relocate(deltaPos, lowerBound, upperBound);
	m_states[1].m_massList.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
float HumanCenterOfMass::PoseDist(const ComPose* a, const ComPose* b)
{
	PROFILE_AUTO(Animation);
	ALWAYS_ASSERT(a);
	ALWAYS_ASSERT(b);
	ALWAYS_ASSERT(a->m_massPos.Size() == b->m_massPos.Size());

	const int numPoints	  = a->m_massPos.Size();
	float sumWeightedDist = 0.0f;
	for (int i = 0; i < numPoints; i++)
	{
		float dist = Dist(a->m_massPos[i], b->m_massPos[i]);
		sumWeightedDist += dist * s_ComTable[i].m_totalWt;
	}

	return sumWeightedDist;
}

/// --------------------------------------------------------------------------------------------------------------- ///
HumanCenterOfMass::ComState::ComState() : m_massList(ARRAY_COUNT(s_ComTable))
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
HumanCenterOfMass::ComPose::ComPose() : m_massPos(ARRAY_COUNT(s_ComTable))
{
	m_massPos.Resize(m_massPos.Capacity());
}

/// --------------------------------------------------------------------------------------------------------------- ///
void HumanCenterOfMass::ComPose::Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound)
{
	m_massPos.Relocate(deltaPos, lowerBound, upperBound);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void HumanCenterOfMass::ComPose::DebugDraw(PrimServerWrapper& prim, Color color) const
{
	STRIP_IN_FINAL_BUILD;

	for (int i = 0; i < m_massPos.Size(); i++)
	{
		float mass	 = s_ComTable[i].m_totalWt;
		float scale	 = 0.05f;
		float radius = Pow(mass, 0.333f) * scale;

		prim.DrawSphere(m_massPos[i], radius, color);
	}
}
