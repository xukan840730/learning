/*
 * Copyright (c) 2014 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#if !FINAL_BUILD

#include "gamelib/gameplay/nd-locatable.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/process/spawn-info.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/render/util/prim.h"

bool g_snapshotTestRunning = false;

FWD_DECL_PROCESS_HANDLE(ProcessSnapshotTest);

class ProcessSnapshotTest : public NdLocatableObject
{
public:

	virtual Err Init(const ProcessSpawnInfo& spawnInfo) override;
	virtual U32F GetMaxStateAllocSize() override { return 0; }
	virtual void ProcessUpdate() override;
	virtual ProcessSnapshot* AllocateSnapshot() const override;

	Vector m_theta;
	Vector m_phi;
	Vector m_radius;
	TimeFrame m_spawnTime;

	static const U32 kNumTestLocatables = 128; //512;
	static MutableProcessSnapshotTestHandle s_hTestLocatables[kNumTestLocatables];
	static Point s_testCenterWs;
};

Point ProcessSnapshotTest::s_testCenterWs = kOrigin;
MutableProcessSnapshotTestHandle ProcessSnapshotTest::s_hTestLocatables[ProcessSnapshotTest::kNumTestLocatables];

PROCESS_REGISTER(ProcessSnapshotTest, NdLocatableObject);

/// --------------------------------------------------------------------------------------------------------------- ///



// x = radius * sin(t * theta + phi)

/// --------------------------------------------------------------------------------------------------------------- ///
Err ProcessSnapshotTest::Init(const ProcessSpawnInfo& spawnInfo)
{
	m_theta = Vector(frand(0.1f, 3.0f), frand(0.1f, 3.0f), frand(0.1f, 3.0f));
	m_phi = Vector(frand(-PI, TAU), frand(-PI, TAU), frand(-PI, TAU));
	m_radius = Vector(frand(-10.0f, 20.0f), frand(-10.0f, 20.0f), frand(-10.0f, 20.0f));
	m_spawnTime = GetCurTime();

	SetAllowThreadedUpdate(true);

	return NdLocatableObject::Init(spawnInfo);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void ProcessSnapshotTest::ProcessUpdate()
{
	const BoundFrame lastLoc = m_boundFrame;

	//m_boundFrame.SetTranslationWs(Point(NAN, NAN, NAN)); // Batman!
	//m_boundFrame.SetRotationWs(Quat(NAN, NAN, NAN, NAN)); // Batman

	memset(&m_boundFrame, NAN, sizeof(BoundFrame)); // Batman.

	const float t = ToSeconds(GetClock()->GetTimePassed(m_spawnTime));

	const float newX = (m_radius.X() * Sin((t * m_theta.X()) + m_phi.X())) + s_testCenterWs.X();
	const float newY = (m_radius.Y() * Sin((t * m_theta.Y()) + m_phi.Y())) + s_testCenterWs.Y();
	const float newZ = (m_radius.Z() * Sin((t * m_theta.Z()) + m_phi.Z())) + s_testCenterWs.Z();

	const Point newPosWs = Point(newX, newY, newZ);

	const Vector moveWs = newPosWs - lastLoc.GetTranslationWs();

	const Quat newRotWs = QuatFromLookAt(SafeNormalize(moveWs, kUnitZAxis), kUnitYAxis);

	const Locator newLocWs = Locator(newPosWs, newRotWs);

	g_prim.Draw(DebugCoordAxes(newLocWs));

	ProcessHandle hSelf(this);

	Point threeClosestWs[3] = { kOrigin, kOrigin, kOrigin };
	float threeClosestDists[3] = { kLargeFloat, kLargeFloat, kLargeFloat };

	int myIndex = -1;

	for (U32F i = 0; i < kNumTestLocatables; ++i)
	{
		if (s_hTestLocatables[i] == hSelf)
		{
			myIndex = i;
			continue;
		}

		const NdLocatableSnapshot* pOtherSnapshot = s_hTestLocatables[i].ToSnapshot<NdLocatableSnapshot>();

		const Point otherPosWs = pOtherSnapshot->m_boundFrame.GetTranslationWs();
		const float dist = Dist(otherPosWs, newPosWs);

		for (U32F j = 0; j < 3; ++j)
		{
			if (threeClosestDists[j] >= dist)
			{
				threeClosestDists[j] = dist;
				threeClosestWs[j] = otherPosWs;
				break;
			}
		}
	}

	g_prim.Draw(DebugString(newPosWs, StringBuilder<64>("%d", myIndex).c_str(), kColorWhite, 0.5f));

	for (U32F i = 0; i < 3; ++i)
	{
		g_prim.Draw(DebugLine(newPosWs, threeClosestWs[i], kColorWhiteTrans, kColorOrange));
	}

	memcpy(&m_boundFrame, &lastLoc, sizeof(BoundFrame));
	m_boundFrame.SetLocatorWs(newLocWs);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ProcessSnapshot* ProcessSnapshotTest::AllocateSnapshot() const
{
	return NdLocatableSnapshot::Create(this);
}

/// --------------------------------------------------------------------------------------------------------------- ///
void StartProcessSnapshotTest()
{
	SpawnInfo spawnInfo(SID("ProcessSnapshotTest"));
	spawnInfo.m_bindSpawner = nullptr;

	for (U32F i = 0; i < ProcessSnapshotTest::kNumTestLocatables; ++i)
	{
		ProcessSnapshotTest* pProc = (ProcessSnapshotTest*)Process::SpawnProcess(spawnInfo);
		ProcessSnapshotTest::s_hTestLocatables[i] = pProc;
	}

	ProcessSnapshotTest::s_testCenterWs = GetRenderCamera(0).m_position;

	g_snapshotTestRunning = true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void StopProcessSnapshotTest()
{
	for (U32F i = 0; i < ProcessSnapshotTest::kNumTestLocatables; ++i)
	{
		ProcessSpawnInfo spawnInfo(SID("ProcessSnapshotTest"));
		KillProcess(ProcessSnapshotTest::s_hTestLocatables[i]);
	}

	g_snapshotTestRunning = false;
}

#endif // !FINAL_BUILD
