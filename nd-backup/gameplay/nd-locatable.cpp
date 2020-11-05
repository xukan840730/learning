/*
 * Copyright (c) 2006 Naughty Dog, Inc.
 * A Wholly Owned Subsidiary of Sony Computer Entertainment, Inc.
 * Use and distribution without consent strictly prohibited
 */

#include "gamelib/gameplay/nd-locatable.h"

#include "ndlib/anim/anim-debug.h"
#include "ndlib/math/pretty-math.h"
#include "ndlib/process/process-mgr.h"
#include "ndlib/process/process-spawn-info.h"
#include "ndlib/process/spawn-info.h"
#include "ndlib/profiling/profiling.h"
#include "ndlib/render/util/screen-space-text-printer.h"

#include "gamelib/gameplay/nd-game-object.h"
#include "gamelib/level/entity-spawner.h"
#include "gamelib/level/level.h"
#include "gamelib/ndphys/rigid-body.h"

PROCESS_REGISTER(NdLocatableObject, Process);

FROM_PROCESS_DEFINE(NdLocatableObject);

/// --------------------------------------------------------------------------------------------------------------- ///
static void ValidateBoundFrame(const BoundFrame& boundFrame)
{
	STRIP_IN_FINAL_BUILD;
	const Point objWsPos = boundFrame.GetTranslation();
	ANIM_ASSERTF(Length(objWsPos) < 100000.0f, ("BoundFrame translation out of range: %s", PrettyPrint(objWsPos)));
	ANIM_ASSERT(IsFinite(objWsPos));
}

/// --------------------------------------------------------------------------------------------------------------- ///
static bool CheckCorrectParent(const Process* pProc, const Process* pParentProc)
{
	if (pParentProc)
	{
		// check that this is a sane thing to do
		ProcessBucket myBucket = EngineComponents::GetProcessMgr()->GetProcessBucket(*pProc);
		ProcessBucket parentBucket = EngineComponents::GetProcessMgr()->GetProcessBucket(*pParentProc);
		if (parentBucket >= myBucket)
		{
			MsgConScriptError("Process %s (bucket %d) is binding itself to process %s (bucket %d) (it is only legal to bind onto a process in an earlier bucket)\n",
							  pProc->GetName(),
							  (int)myBucket,
							  pParentProc->GetName(),
							  (int)parentBucket);
			return false;
		}
	}
	return true;
}

/// --------------------------------------------------------------------------------------------------------------- ///
static StringId64 LookupBindSpawnerNameId(const RigidBody* pBindBody)
{
	StringId64 id = INVALID_STRING_ID_64;
	if (pBindBody)
	{
		if (const NdGameObject* pBindGo = pBindBody->GetOwner())
		{
			if (const EntitySpawner* pBindSpawner = pBindGo->GetSpawner())
			{
				id = pBindSpawner->NameId();
			}
		}
	}
	return id;
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdLocatableObject::NdLocatableObject()
	: m_pSpawner(nullptr)
	, m_bindSpawnerNameId(INVALID_STRING_ID_64)
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdLocatableObject::~NdLocatableObject()
{
}

/// --------------------------------------------------------------------------------------------------------------- ///
U32F NdLocatableObject::GetMaxStateAllocSize()
{
	if (IsRawType())
		return 0; // I'm a "raw" NdLocatableObject, not a derived class; I need no state.
	else
		return ParentClass::GetMaxStateAllocSize();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLocatableObject::RefreshSnapshot(ProcessSnapshot* pSnapshot) const
{
	NdLocatableSnapshot* pLocatableSnapshot = static_cast<NdLocatableSnapshot*>(pSnapshot); //NdLocatableSnapshot::FromSnapshot(pSnapshot);
	//if ()
	{
		pLocatableSnapshot->m_bindSpawnerNameId = m_bindSpawnerNameId;
		pLocatableSnapshot->m_boundFrame = m_boundFrame;
		pLocatableSnapshot->m_pSpawner = m_pSpawner;
		pLocatableSnapshot->m_navLocation = GetNavLocation();
	}

	ParentClass::RefreshSnapshot(pSnapshot);
}

/// --------------------------------------------------------------------------------------------------------------- ///
ProcessSnapshot* NdLocatableObject::AllocateSnapshot() const
{
#if REDUCED_SNAPSHOTS
	return nullptr;
#else
	return NdLocatableSnapshot::Create(this);
#endif
}

/// --------------------------------------------------------------------------------------------------------------- ///
Err NdLocatableObject::Init(const ProcessSpawnInfo& spawnInfo)
{
	PROFILE(Processes, NdLocatableObject_Init);

	SpawnInfo& spawn = (SpawnInfo&)spawnInfo;

	m_bindSpawnerNameId = INVALID_STRING_ID_64;
	if (const EntitySpawner* pBindSpawner = spawn.m_bindSpawner)
	{
		m_bindSpawnerNameId = pBindSpawner->NameId();
	}
	// This is the id of a joint in the parent object to which I should be parented.
	// (Only meaningful if I have a parent spawner.)
	StringId64 bindJointId = spawn.GetData<StringId64>(SID("ParentToJoint"), INVALID_STRING_ID_64);

	if (spawn.m_pSpawner != nullptr && spawn.m_pSpawner->GetParentSpawner() != nullptr)
	{
		if (spawn.m_pSpawner->GetBinding().GetRigidBody() == nullptr)
		{
			MsgConScriptError("unable to parent %s to %s at joint %s\n",
							 spawn.m_pSpawner->Name(),
							 spawn.m_pSpawner->GetParentSpawner()->Name(),
							 DevKitOnly_StringIdToString(bindJointId));
		}
	}

	// possibly attach to the spawner
	if (spawn.m_pSpawner && !spawn.m_pSpawner->GetFlags().m_orphaned)
	{
		m_pSpawner = spawn.m_pSpawner;
	}

	Err result = ParentClass::Init(spawn);

	if (result.Succeeded())
	{
		BindToRigidBody(nullptr);

		InitLocator(spawn);

		switch (spawn.GetData<StringId64>(SID("DrawBucket"), SID("Unspecified")).GetValue())
		{
		case SID_VAL("MainDriver"):
			ChangeParentProcess(EngineComponents::GetProcessMgr()->m_pMainDriverTree);
			break;
		case SID_VAL("Platform"):
			ChangeParentProcess(EngineComponents::GetProcessMgr()->m_pPlatformTree);
			break;
		case SID_VAL("Character"):
			ChangeParentProcess(EngineComponents::GetProcessMgr()->m_pCharacterTree);
			break;
		case SID_VAL("Attach"):
			ChangeParentProcess(EngineComponents::GetProcessMgr()->m_pAttachTree);
			break;
		case SID_VAL("SubAttach"):
			ChangeParentProcess(EngineComponents::GetProcessMgr()->m_pSubAttachTree);
			break;
		case SID_VAL("DefaultForce"):
			ChangeParentProcess(EngineComponents::GetProcessMgr()->m_pDefaultTree);
			break;
		}

		if (spawn.m_bindSpawner)
		{
			if (const NdGameObject* pBindGo = spawn.m_bindSpawner->GetNdGameObject())
			{
				if (const RigidBody* pBindBody = pBindGo->GetJointBindTarget(bindJointId))
				{
					// don't call virtually or partially constructed objects like NPCs will crash
					NdLocatableObject::BindToRigidBody(pBindBody);
				}
				else
				{
					MsgCon("Spawned entity \"%s\" when bind spawner \"%s\" is NOT spawned or is invalid\n",
						   GetName(),
						   spawn.m_bindSpawner->Name());
					GoError("Spawned entity \"%s\" when bind spawner %s doesn't have valid RigidBody\n",
							GetName(),
							spawn.m_bindSpawner->Name());
					ASSERTF(false,
							("Bind spawner %s doesn't have valid RigidBody at joint %s\n",
							 spawn.m_bindSpawner->Name(),
							 DevKitOnly_StringIdToString(bindJointId)));
					return Err::kErrAbort;
				}
			}
			else
			{
				MsgCon("Spawned entity \"%s\" when bind spawner \"%s\" is NOT spawned or is invalid\n",
					   GetName(),
					   spawn.m_bindSpawner->Name());
				GoError("Spawned entity \"%s\" when bind spawner \"%s\" is NOT spawned or is invalid\n",
						GetName(),
						spawn.m_bindSpawner->Name());
				ASSERTF(false,
						("Spawning entity \"%s\" when bind spawner \"%s\" is NOT spawned or is invalid\n",
						 GetName(),
						 spawn.m_bindSpawner->Name()));
				return Err::kErrAbort;
			}
		}

		if (IsRawType())
		{
			// I'm a "raw" NdLocatableObject, not a derived class.
			SetUpdateEnabled(false);
			SetUserId(spawn.BareNameId(GetUserBareId()), spawn.NamespaceId(GetUserNamespaceId()), spawn.NameId(GetUserId()));
		}
	}

	return result;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLocatableObject::InitLocator(const ProcessSpawnInfo& spawn)
{
	if (spawn.m_pRoot)
	{
		Locator spawnLoc = *spawn.m_pRoot;

		GAMEPLAY_ASSERT(IsReasonable(spawnLoc));

		// It seems that sometimes a spawner can have a denormalized quaternion.
		spawnLoc.SetRot(Normalize(spawnLoc.Rot()));

		SetLocator(spawnLoc);
	}
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdLocatableObject::SetBoundFrame(const BoundFrame& frame)
{
#if !FINAL_BUILD
	const RigidBody* pParentBody = frame.GetBinding().GetRigidBody();
	if (pParentBody)
	{
		CheckCorrectParent(this, pParentBody->GetOwner());
	}
#endif
	ValidateBoundFrame(frame);

	if (m_boundFrame.GetBinding().IsSameBinding(frame.GetBinding()))
	{
		m_boundFrame = frame;
	}
	else
	{
		Locator newLocWs = frame.GetLocatorWs();
		SetBinding(frame.GetBinding());
		m_boundFrame.SetLocatorWs(newLocWs);
	}

	OnLocationUpdated();
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdLocatableObject::SetLocator(const Locator& loc)
{
	m_boundFrame.SetLocator(loc);
	ValidateBoundFrame(m_boundFrame);

	OnLocationUpdated();
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdLocatableObject::SetLocatorPs(const Locator& loc)
{
	m_boundFrame.SetLocatorPs(loc);
	ValidateBoundFrame(m_boundFrame);

	OnLocationUpdated();
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdLocatableObject::SetTranslation(Point_arg trans)
{
	m_boundFrame.SetTranslation(trans);
	ValidateBoundFrame(m_boundFrame);

	OnLocationUpdated();
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdLocatableObject::SetTranslationPs(Point_arg pt)
{
	m_boundFrame.SetTranslationPs(pt);
	ValidateBoundFrame(m_boundFrame);

	OnLocationUpdated();
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdLocatableObject::AdjustTranslation(Vector_arg move)
{
	m_boundFrame.AdjustTranslation(move);
	ValidateBoundFrame(m_boundFrame);

	OnLocationUpdated();
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdLocatableObject::SetRotation(Quat_arg rot)
{
	ASSERT(IsFinite(rot));
	m_boundFrame.SetRotation(rot);

	OnLocationUpdated();
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdLocatableObject::SetRotationPs(Quat_arg rot)
{
	m_boundFrame.SetRotationPs(rot);

	OnLocationUpdated();
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdLocatableObject::BindToRigidBody(const RigidBody* pParentBody)
{
	PROFILE(Processes, NdLocObj_BindToRigidBody);

	if (IsSameBindSpace(pParentBody))
		return;

	RigidBody* pOldParentBody = GetBoundRigidBody();
	const RigidBody* pNewParentBody = pOldParentBody;
	const NdGameObject* pBindGo = pParentBody ? pParentBody->GetOwner() : nullptr;

	if (pBindGo == this) // CheckCorrectParent() will also catch this but we'll handle it silently instead
		return;

	if (CheckCorrectParent(this, pBindGo))
	{
		// if this external is not background,
		pNewParentBody = pParentBody;
	}

	const Binding& oldBinding = GetBoundFrame().GetBinding();
	const Binding newBinding = Binding(pNewParentBody);

	if (oldBinding.IsSameBinding(newBinding))
		return;

	OnRemoveParentRigidBody(pOldParentBody, pNewParentBody);

	Locator oldParentSpace = GetParentSpace();
	m_boundFrame.SetBinding(Binding(pNewParentBody));
	m_bindSpawnerNameId = INVALID_STRING_ID_64;

	const EntitySpawner* pBindSpawner = (pBindGo && pNewParentBody) ? pBindGo->GetSpawner() : nullptr;

	if (pBindSpawner)
	{
		m_bindSpawnerNameId = pBindSpawner->NameId();
	}

	if (pNewParentBody)
	{
		OnAddParentRigidBody(pOldParentBody, oldParentSpace, pNewParentBody);
	}

	// check that our cached value (m_bindSpawnerNameId) agrees with the old logic
	ASSERT(pNewParentBody == nullptr || LookupBindSpawnerNameId(pNewParentBody) == m_bindSpawnerNameId);
}

/// --------------------------------------------------------------------------------------------------------------- ///
bool NdLocatableObject::IsSameBindSpace(const RigidBody* pBindTarget) const
{
	return m_boundFrame.IsSameBinding(Binding(pBindTarget));
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLocatableObject::OnRemoveParentRigidBody(RigidBody* pOldParentBody, const RigidBody* pNewParentBody)
{
	// Derived classes may override.
}


/// --------------------------------------------------------------------------------------------------------------- ///
void NdLocatableObject::OnAddParentRigidBody(const RigidBody* pOldParentBody,
											 Locator oldParentSpace,
											 const RigidBody* pNewParentBody)
{
	// Derived classes may override.
}

/// --------------------------------------------------------------------------------------------------------------- ///
const RigidBody* NdLocatableObject::GetBoundRigidBody() const
{
	return m_boundFrame.GetBinding().GetRigidBody();
}

/// --------------------------------------------------------------------------------------------------------------- ///
RigidBody* NdLocatableObject::GetBoundRigidBody()
{
	return m_boundFrame.GetBinding().GetRigidBody();
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLocatableObject::SetBinding(const Binding &binding)
{
	// Just hack this in for now.
	const RigidBody* pRigidBody = binding.GetRigidBody();
	BindToRigidBody(pRigidBody);
}

/// --------------------------------------------------------------------------------------------------------------- ///
Binding NdLocatableObject::GetBinding() const
{
	return m_boundFrame.GetBinding();
}

/// --------------------------------------------------------------------------------------------------------------- ///
NdGameObject* NdLocatableObject::GetBoundGameObject() const
{
	NdGameObject* pBindGo = nullptr;
	if (const RigidBody* pBindBody = GetBoundRigidBody())
	{
		pBindGo = const_cast<NdGameObject*>(pBindBody->GetOwner());
	}
	return pBindGo;
}

/// --------------------------------------------------------------------------------------------------------------- ///
StringId64 NdLocatableObject::GetSpawnerId() const
{
	return m_pSpawner ? m_pSpawner->NameId() : INVALID_STRING_ID_64;
}

/// --------------------------------------------------------------------------------------------------------------- ///
void NdLocatableObject::AssociateWithLevel(const Level* pLevel)
{
	ParentClass::AssociateWithLevel(pLevel);

	// cleanup m_pSpawner pointer if process is no longer associated with a level.
	if (GetAssociatedLevel() == nullptr)
	{
		m_pSpawner = nullptr;
	}
}

void NdLocatableObject::UpdateFocusTargetTravelDist(float& travelDist, BoundFrame& lastFrameFocusBf) const
{
	if (GetBinding().IsSameBinding(lastFrameFocusBf.GetBinding()))
		travelDist += Dist(GetTranslationPs(), lastFrameFocusBf.GetTranslationPs());
	lastFrameFocusBf = GetBoundFrame();
}

/// --------------------------------------------------------------------------------------------------------------- ///
// Snapshot
/// --------------------------------------------------------------------------------------------------------------- ///

PROCESS_SNAPSHOT_DEFINE(NdLocatableSnapshot, ProcessSnapshot);

/// --------------------------------------------------------------------------------------------------------------- ///
// Debugging
/// --------------------------------------------------------------------------------------------------------------- ///

#include "ndlib/camera/camera-final.h"
#include "ndlib/render/render-camera.h"
#include "ndlib/render/util/prim.h"

void NdLocatableObject::DebugShowProcess(ScreenSpaceTextPrinter& printer) const
{
	STRIP_IN_FINAL_BUILD;

	const Point& pos = GetLocator().Pos();

	float sx, sy;
	if (GetRenderCamera(0).WorldToVirtualScreen(sx, sy, pos, true))
	{
		DebugDrawCross2D(Vec2(sx, sy), kDebug2DLegacyCoords, 5.0f, kColorWhite);
	}

	g_prim.Draw(DebugCoordAxes(GetLocator(), 0.5f));

	StringId64 userId = GetUserId();

	const float dist = Length(pos - g_mainCameraInfo[0].GetPosition());

	const Process* pParent = GetParentProcess();

	//TextPrinterWorldSpace printer;
	//printer.Start(pos);

	char updateFlags[8] = { '-', '-', '-', '-', '-', '-', '-', '\0' };
	GetUpdateFlagString(updateFlags);

	printer.PrintText(kColorYellow,
					  "%s (pid %u) %dm %s [p: %s]",
					  DevKitOnly_StringIdToString(userId),
					  (U32)GetProcessId(),
					  (int)dist,
					  updateFlags,
					  pParent ? pParent->GetName() : "<null>");

	if (dist < 25.0f)
	{
		if (const Level* pLevel = GetAssociatedLevel())
		{
			printer.PrintText(kColorCyan, "Level: %s\n\n", pLevel->GetName());
		}

		const char* strType = GetTypeName();
		if (!strType)
			strType = "unknown";

		printer.PrintText(kColorWhite, "%s \"%s\"", strType, GetName());
	}
}
