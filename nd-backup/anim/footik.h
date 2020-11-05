/*
 * Copyright (c) 2003 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#pragma once

#include "ndlib/anim/ik/ik-defs.h"
#include "ndlib/anim/ik/joint-chain.h"

class ArtItemSkeleton;
class Locator;
class NdGameObject;

namespace OrbisAnim
{
	struct ValidBits;
}
namespace ndanim
{
	struct JointParams;
}

class LegIkChain : public JointTree
{
public:
	enum
	{
		kRoot,
		kHip,
		kKnee,
		kAnkle,
		kHeel,
		kBall,

		kJointCount
	};

	static const StringId64 kHumanJointIds[kQuadLegCount][kJointCount];
	static const StringId64 kHorseJointIds[kQuadLegCount][kJointCount];
	static const StringId64 kDogJointIds[kQuadLegCount][kJointCount];

	typedef StringId64 JointName2DArray[kQuadLegCount][kJointCount];

	//returns pointer to array of array of SIDs
	static const JointName2DArray* GetJointIdsForCharType(FootIkCharacterType charType);

private:
	I8	m_legIndex;
	I8	m_jointOffsets[kJointCount];

public:
	LegIkChain();

	virtual bool Init(NdGameObject* pGo) override
	{
		ANIM_ASSERTF(false, ("this variant of Init() it not intended to be used for LegIkChain"));
		return JointTree::Init(pGo);
	}
	virtual bool Init(NdGameObject* pGo, StringId64 rootJoint, bool useAnimatedSkel, int numEndJoints, ...) override
	{
		ANIM_ASSERTF(false, ("this variant of Init() it not intended to be used for LegIkChain"));
		return JointTree::Init(pGo);
	}
	virtual bool Init(NdGameObject* pGo,
					  StringId64 rootJoint,
					  bool useAnimatedSkel,
					  int numEndJoints,
					  const StringId64* endJointNames) override
	{
		ANIM_ASSERTF(false, ("this variant of Init() it not intended to be used for LegIkChain"));
		return JointTree::Init(pGo);
	}
	virtual bool Init(const ArtItemSkeleton* pSkeleton,
					  StringId64 rootJoint,
					  int numEndJoints,
					  const StringId64* endJointNames) override
	{
		ANIM_ASSERTF(false, ("this variant of Init() it not intended to be used for LegIkChain"));
		return JointTree::Init(nullptr);
	}

	bool Init(NdGameObject *pGo, int legIndex, bool includeRoot = false);			// Allocates memory
	bool Init(const ArtItemSkeleton* pSkel, FootIkCharacterType charType, int legIndex, bool includeRoot = false);		// Allocates memory

	virtual void ReadJointCache();
	bool ReadFromJointParams(const ndanim::JointParams* pJointParamsLs,
		U32F indexBase,
		U32F numJoints,
		float rootScale,
		const OrbisAnim::ValidBits* pValidBits = nullptr); // allocates memory

	int RootOffset() const				{ return m_jointOffsets[kRoot]; }
	int HipOffset() const				{ return m_jointOffsets[kHip]; }
	int KneeOffset() const				{ return m_jointOffsets[kKnee]; }
	int AnkleOffset() const				{ return m_jointOffsets[kAnkle]; }
	int HeelOffset() const				{ return m_jointOffsets[kHeel]; }
	int BallOffset() const				{ return m_jointOffsets[kBall]; }

	int RootJointIndex() const			{ return GetJointIndex(m_jointOffsets[kRoot]); }
	int HipJointIndex() const			{ return GetJointIndex(m_jointOffsets[kHip]); }
	int KneeJointIndex() const			{ return GetJointIndex(m_jointOffsets[kKnee]); }
	int AnkleJointIndex() const			{ return GetJointIndex(m_jointOffsets[kAnkle]); }
	int HeelJointIndex() const			{ return GetJointIndex(m_jointOffsets[kHeel]); }
	int BallJointIndex() const			{ return GetJointIndex(m_jointOffsets[kBall]); }

	const Locator& GetRootLocWs()		{ return GetJointLocWs(m_jointOffsets[kRoot]); }
	const Locator& GetHipLocWs()		{ return GetJointLocWs(m_jointOffsets[kHip]); }
	const Locator& GetKneeLocWs()		{ return GetJointLocWs(m_jointOffsets[kKnee]); }
	const Locator& GetAnkleLocWs()		{ return GetJointLocWs(m_jointOffsets[kAnkle]); }
	const Locator& GetHeelLocWs()		{ return GetJointLocWs(m_jointOffsets[kHeel]); }
	const Locator& GetBallLocWs()		{ return GetJointLocWs(m_jointOffsets[kBall]); }

	const Locator& GetHipLocLs()		{ return GetJointLocLs(m_jointOffsets[kHip]); }
	const Locator& GetKneeLocLs()		{ return GetJointLocLs(m_jointOffsets[kKnee]); }
	const Locator& GetAnkleLocLs()		{ return GetJointLocLs(m_jointOffsets[kAnkle]); }
	const Locator& GetHeelLocLs()		{ return GetJointLocLs(m_jointOffsets[kHeel]); }
	const Locator& GetBallLocLs()		{ return GetJointLocLs(m_jointOffsets[kBall]); }

	Quat RotateRootWs(Quat_arg rot)		{ return RotateJointWs(m_jointOffsets[kRoot], rot); }
	Quat RotateHipWs(Quat_arg rot)		{ return RotateJointWs(m_jointOffsets[kHip], rot); }
	Quat RotateKneeWs(Quat_arg rot)		{ return RotateJointWs(m_jointOffsets[kKnee], rot); }
	Quat RotateAnkleWs(Quat_arg rot)	{ return RotateJointWs(m_jointOffsets[kAnkle], rot); }
	Quat RotateHeelWs(Quat_arg rot)		{ return RotateJointWs(m_jointOffsets[kHeel], rot); }
	Quat RotateBallWs(Quat_arg rot)		{ return RotateJointWs(m_jointOffsets[kBall], rot); }

	const Vector HipAxis() const			{ return m_jointData->m_hipAxis; }
	const Vector HipDown() const			{ return m_jointData->m_hipDown; }

	float GetKneeAngle();
	void SetKneeAngle(float kneeAngle);

};

struct LegIkInstance
{
	LegIkInstance();

	JointSet*		m_ikChain;
	int				m_legIndex;		// only needed if m_pJointSet is not type LegIkChain

	Point			m_goalPos;
	Point			m_outputGoalPos;
	bool			m_useOld;

	bool			m_havePoleVec;
	Vector			m_poleVectorWs;	// plane where IK gets solved.

	float			m_kneeLimitMin;
	float			m_kneeLimitMax;
	float			m_thighLimitMin;
	float			m_thighLimitMax;
};

void SolveLegIk(LegIkInstance* ik);

void ComputeHipData(const ArtItemSkeleton* pSkel, int hipIndex, Vector* hipAxis, Vector* hipDown);
