/*
 * Copyright (c) 2004 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited.
 */

#include "gamelib/ndphys/rope/rope-process.h"

#include <shared/math/mat44.h>
#include "ndlib/process/process.h"
#include "ndlib/render/util/prim.h"
#include "ndlib/process/process-mgr.h"
#include "gamelib/ndphys/rope/rope-mgr.h"
#include "gamelib/ndphys/rope/rope2.h"
#include "gamelib/ndphys/rope/physvectormath.h"
#include "ndlib/process/spawn-info.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/render-camera.h"
#include "gamelib/ndphys/collide-utils.h" 
#include "ndlib/nd-game-info.h"
#include "ndlib/nd-config.h"
#include "ndlib/nd-frame-state.h"

PROCESS_REGISTER_ALLOC_SIZE(RopeProcess, NdLocatableObject, 1024 * 1024);

static const F32 kRopeSegmentLength = 0.2f;

Err RopeProcess::Init(const ProcessSpawnInfo& spawn)
{
	F32 fLength = spawn.GetData<F32>(SID("ropeLength"), 10.0f);
	bool bSleeping = spawn.GetData<bool>(SID("physStartSleeping"), false);
	F32 fSegmentLength = spawn.GetData<F32>(SID("physJointSpacing"), kRopeSegmentLength);
	F32 fPinnedDist = spawn.GetData<F32>(SID("pinnedDist"), -1.0f);
	m_bPinnedStrained = spawn.GetData<bool>(SID("pinnedStrained"), false);

	m_draggedRopeDist = -1.0f;
	m_pinnedRopeDist = -1.0f;

	// Put into the "attach" tree so that ropes get updated after the player and NPCs.
	// This enables rope grabs to look correct (i.e. not be one frame off).
	ChangeParentProcess( EngineComponents::GetProcessMgr()->m_pAttachTree );

	Err result = ParentClass::Init(spawn);	

	m_bInited = false;

	if (!result.Succeeded())
		return result;

	Point endPt = GetLocator().Pos() - fLength * GetLocalY(GetLocator().GetRotation());

	if (fPinnedDist >= 0.0f)
	{
		m_pinnedRopeDist = fLength;
		m_pinnedRopePoint = GetLocator().TransformPoint(Point(0.0f, -fPinnedDist, 0.0f));
		endPt = m_pinnedRopePoint;
	}

	// initialize the rope
	Rope2InitData initData;
	initData.m_length = fLength;
	initData.m_radius = 0.04f;
	initData.m_segmentLength = fSegmentLength;
	m_rope.Init(initData);

	m_rope.m_pOwner = this;
	m_rope.InitStraightPose(GetLocator().Pos(), endPt);

	m_rope.m_bSleeping = bSleeping;

	m_rope.InitRopeDebugger();

	g_ropeMgr.RegisterRope(&m_rope);
	m_bInited = true;

	return result;
}

void RopeProcess::Relocate(ptrdiff_t delta, uintptr_t lowerBound, uintptr_t upperBound)
{
	if (m_bInited)
		g_ropeMgr.RelocateRope(&m_rope, delta, lowerBound, upperBound);
	m_rope.Relocate(delta, lowerBound, upperBound);
	ParentClass::Relocate(delta, lowerBound, upperBound);
}

void RopeProcess::EventHandler(Event& event)
{
	ParentClass::EventHandler(event);

	switch (event.GetMessage().GetValue())
	{
	case SID_VAL("deactivate"):
		{
			m_rope.m_bSleeping = true;
		}
		break;
	case SID_VAL("activate"):
		{
			m_rope.m_bSleeping = false;
		}
		break;
	}
}

void RopeProcess::OnKillProcess()
{
	if (m_bInited)
	{
		g_ropeMgr.UnregisterRope(&m_rope);
		m_rope.Destroy();
	}
	ParentClass::OnKillProcess();
}


static F32 g_mouseGrabDist;

void RopeProcess::ProcessMouse()
{
	Mouse &mouse = EngineComponents::GetNdFrameState()->m_mouse;
	RenderCamera const &cam = GetRenderCamera(0);

	float fMarkerRadius = 0.2f;
	if(m_draggedRopeDist >= 0.0f)
	{
		// find a point in the camera's left/up plane going through current refPos
		Vec3 pos, dir;
		cam.VirtualScreenToWorld(pos, dir, mouse.m_position.X(), mouse.m_position.Y());
		Point newPos = cam.m_position + Vector(dir) * g_mouseGrabDist;
		g_prim.Draw( DebugSphere(Sphere(newPos, fMarkerRadius), g_colorCyan));
		m_rope.SetKeyframedPos(m_draggedRopeDist, newPos);
		if(!(mouse.m_buttons & kMouseButtonLeft))
		{
			if (mouse.m_buttons & kMouseButtonRight)
			{
				m_pinnedRopeDist = m_draggedRopeDist;
				m_pinnedRopePoint = newPos;
			}
			m_draggedRopeDist = -1.0f;
		}
		m_rope.WakeUp();
	}
	else
	{
		Scalar scBestSqR(25.0f);
		F32 bestRopeDist = -1.0f;
		for (U32F ii = 1; ii<m_rope.GetNumSimPoints(); ii++)
		{
			Point ropePos = m_rope.GetSimPos()[ii];
			if (cam.IsPointInFrustum(ropePos))
			{
				F32 sX, sY;
				cam.WorldToVirtualScreen(sX, sY, ropePos);
				Scalar scSqR = Sqr(sX-mouse.m_position.X()) + Sqr(sY-mouse.m_position.Y());
				if(scSqR < scBestSqR)
				{
					scBestSqR = scSqR;
					bestRopeDist = m_rope.GetSimRopeDist()[ii];
				}
			}
		}
		if (bestRopeDist >= 0.0f)
		{
			// select this object
			g_prim.Draw( DebugSphere(Sphere(m_rope.GetPos(bestRopeDist), fMarkerRadius), g_colorYellow));

			if(mouse.m_buttonsPressed & kMouseButtonLeft)
			{
				if (Abs(m_pinnedRopeDist - bestRopeDist) < 0.01f)
				{
					m_pinnedRopeDist = -1.0f;
				}
				m_draggedRopeDist = bestRopeDist;
				g_mouseGrabDist = Dist(m_rope.GetPos(bestRopeDist), cam.m_position);
				m_rope.SetKeyframedPos(m_draggedRopeDist, m_rope.GetPos(bestRopeDist));
				m_bPinnedStrained = false;
			}
		}
	}
}

class RopeProcess::Active : public Process::State
{
	BIND_TO_PROCESS(RopeProcess);

public:
	virtual void Update() override
	{
		PROFILE(Processes, RopeProcess_Update);

		RopeProcess& pp = Self();
		Process::State::Update();

		pp.m_rope.SetKeyframedPos(0.0f, pp.GetTranslation());
		if (pp.m_pinnedRopeDist >= 0.0f)
		{
			pp.m_rope.SetKeyframedPos(pp.m_pinnedRopeDist, pp.m_pinnedRopePoint, pp.m_bPinnedStrained ? Rope2::kNodeStrained|Rope2::kNodeUserMark1 : 0);
		}
		pp.m_rope.Step();
		pp.m_rope.ClearAllKeyframedPos();
		pp.SetTranslation(pp.m_rope.GetRoot());
		pp.ProcessMouse();
		pp.m_rope.WakeUp(); // never sleep, this is for debug

		// 	if(g_settings.m_physCursor)
		// 		pp.ProcessMouse();
	}
};

STATE_REGISTER(RopeProcess, Active, kPriorityNormal);
