/*
 * Copyright (c) 2012 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "corelib/containers/list-array.h"
#include "corelib/util/timeframe.h"

/// --------------------------------------------------------------------------------------------------------------- ///
class AttachSystem;
class ArtItemAnim;
class PrimServerWrapper;

/// --------------------------------------------------------------------------------------------------------------- ///
class HumanCenterOfMass
{
public:
	struct COMEntry
	{
		StringId64 m_jointId;
		Point m_comOffset;
		float m_totalWt;
	};

	class ComPose
	{
	public:
		void Relocate(ptrdiff_t deltaPos, uintptr_t lowerBound, uintptr_t upperBound);
		void DebugDraw(PrimServerWrapper& prim, Color color) const;

		Point GetCOM() const { return m_comPos; }

	private:
		ComPose();

		ComPose(ComPose& other);
		ComPose& operator=(const ComPose& other);

		ListArray<Point> m_massPos;
		Point m_comPos;

		friend class HumanCenterOfMass;
	};

	static Point ComputeCenterOfMassWs(const AttachSystem& attachSystem,
									   const COMEntry* pWeights = nullptr,
									   const int numWeights		= 0);
	static Point ComputeCenterOfMassFromJoints(SkeletonId skelId,
											   const Transform* aJointTransforms,
											   ListArray<Point>* pMassList = nullptr);
	static Point ComputeCenterOfMassFromJoints(SkeletonId skelId, const Locator* aJointLocs);
	static Point GetCenterOfMassFromAnimation(SkeletonId skelId,
											  const ArtItemAnim* pAnim,
											  float phase,
											  bool mirror,
											  ListArray<Point>* pMassList = nullptr);
	static ComPose* GetCOMPoseFromAnimation(SkeletonId skelId,
											const ArtItemAnim* pAnim,
											float phase,
											bool mirror);										 // allocates memory
	static ComPose* GetCOMPoseFromAttachSystem(const AttachSystem& attachSystem, Locator align); // allocates memory
	static float PoseDist(const ComPose* a, const ComPose* b);

	HumanCenterOfMass();
	void Relocate(ptrdiff_t offset_bytes, uintptr_t lowerBound, uintptr_t upperBound);
	void Update(const AttachSystem& attachSystem);
	Point GetCOM() const;
	Vector GetAngularMomentum() const;

	void DebugDraw() const;

private:
	static Point ComputeCenterOfMassWs(const AttachSystem& attachSystem,
									   ListArray<Point>* pMassList,
									   const COMEntry* pWeights,
									   const int numWeights);

	struct ComState
	{
		ComState();

		ListArray<Point> m_massList;
		TimeFrame m_time;
		Point m_centerOfMass;
		Vector m_angularMomentum;
		Vector m_comVelocity;
		bool m_valid;
	};

	ComState m_states[2];
	int m_currentBuffer;
};
